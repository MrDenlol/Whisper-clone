#include "AppConfig.h"
#include "Autostart.h"
#include "ModelLocator.h"
#include "PhraseHistory.h"
#include "SessionGuard.h"
#include "Settings.h"
#include "SpeechGate.h"
#include "TextNormalizer.h"
#include "Transcriber.h"
#include "Vad.h"
#include "WavFile.h"

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(WHISPERFLOW_HAS_MICROPHONE)
#include "AudioBuffer.h"
#include "AudioCapture.h"
#include "Hotkey.h"
#include "Overlay.h"
#include "TextInjector.h"
#include "TrayIcon.h"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

enum class ExitCode {
    Ok = 0,
    BadArguments = 2,
    ModelMissing = 3,
    TranscriptionFailed = 4,
    CaptureFailed = 5,
    HotkeyFailed = 6,
};

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Used by the microphone/hotkey dictation path (Windows). Marked maybe_unused so
// the non-microphone build (e.g. Linux CI, WAV-only) does not trip -Werror=unused-function.
[[maybe_unused]] void logLine(const std::string& text) {
    std::cout << text << std::endl;
}

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::error_code ec;
        return std::filesystem::path(buffer).parent_path();
    }
    return {};
#else
    return std::filesystem::current_path();
#endif
}

void printTiming(const whisperflow::TranscriptionResult& result, double captureMs) {
    std::cout << "\n--- Timing ---\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Audio:      " << std::setprecision(2) << result.audioSeconds << " s\n";
    std::cout << std::setprecision(0);
    std::cout << "Capture:    " << captureMs << " ms\n";
    std::cout << "Model load: " << result.modelLoadMs << " ms"
              << (result.modelLoadMs > 0.0 ? "" : " (warm)") << '\n';
    std::cout << "Encode:     " << result.encodeMs << " ms\n";
    std::cout << "Decode:     " << result.decodeMs << " ms\n";
    std::cout << "Inference:  " << result.inferenceMs << " ms\n";
    if (result.audioSeconds > 0.0) {
        std::cout << std::setprecision(2);
        std::cout << "Real-time:  " << (result.inferenceMs / 1000.0) / result.audioSeconds
                  << "x  (inference time / audio time, lower is better)\n";
    }
    std::cout << std::setprecision(0);
}

int transcribeBuffer(std::vector<float> pcm, double captureMs, whisperflow::Transcriber& transcriber,
                     const std::string& languageHint,
                     const whisperflow::PunctuationDictionary& dictionary) {
    int exitCode = static_cast<int>(ExitCode::Ok);
    std::string failure;

    transcriber.transcribe(pcm, [&](const whisperflow::TranscriptionResult& result) {
        if (!result.ok) {
            failure = result.error;
            exitCode = static_cast<int>(ExitCode::TranscriptionFailed);
            return;
        }

        const std::string text = whisperflow::normalizeTranscript(result.text, dictionary);
        std::cout << "\n--- Recognized text ---\n" << text << "\n";
        if (!result.language.empty()) {
            std::cout << "(language: " << result.language;
            if (!languageHint.empty() && languageHint != "auto") {
                std::cout << ", requested: " << languageHint;
            }
            std::cout << ")\n";
        }
        printTiming(result, captureMs);
    });

    if (!failure.empty()) {
        std::cerr << "[Transcriber] " << failure << '\n';
    }
    return exitCode;
}

int runFromWavFile(const whisperflow::AppConfig& config, whisperflow::Transcriber& transcriber,
                   const whisperflow::PunctuationDictionary& dictionary) {
    whisperflow::WavAudio audio;
    std::string error;

    const auto start = Clock::now();
    if (!whisperflow::readWavAsMono16k(config.wavInput, audio, error)) {
        std::cerr << "[WAV] " << error << '\n';
        return static_cast<int>(ExitCode::CaptureFailed);
    }
    const double readMs = msSince(start);

    std::cout << "[WAV] " << config.wavInput.string() << " - " << audio.sourceSampleRate << " Hz, "
              << audio.sourceChannels << " ch -> " << audio.samples.size() << " samples @ 16 kHz\n";

    if (config.vad) {
        const whisperflow::VadResult vad =
            whisperflow::detectSpeech(audio.samples, whisperflow::Transcriber::kSampleRate);
        if (vad.hasSpeech) {
            std::cout << std::fixed << std::setprecision(0)
                      << "[VAD] kept " << std::setprecision(2) << vad.keptSeconds << " s of "
                      << vad.originalSeconds << " s (cut " << std::setprecision(0)
                      << (vad.trimmedLeadingMs + vad.trimmedTrailingMs) << " ms of silence)\n";
            audio.samples = std::vector<float>(
                audio.samples.begin() + static_cast<std::ptrdiff_t>(vad.startSample),
                audio.samples.begin() + static_cast<std::ptrdiff_t>(vad.endSample));
        }
    }

    return transcribeBuffer(std::move(audio.samples), readMs, transcriber, config.language,
                            dictionary);
}

int listInstalledModels(const whisperflow::ModelQuery& query) {
    const std::vector<std::filesystem::path> dirs =
        whisperflow::defaultSearchDirectories(query.executableDirectory);

    std::cout << "Models are looked up in this order:\n";
    for (const std::filesystem::path& dir : dirs) {
        std::cout << "  " << dir.string() << '\n';
    }

    std::cout << "\nInstalled:\n";
    bool anyFound = false;
    for (const std::string& name : whisperflow::allModelSizeNames()) {
        whisperflow::ModelQuery sizeQuery = query;
        whisperflow::ModelSize size{};
        if (!whisperflow::parseModelSize(name, size)) {
            continue;
        }
        sizeQuery.size = size;
        sizeQuery.explicitPath.clear();

        const whisperflow::ModelSearch search = whisperflow::locateModel(sizeQuery);
        if (search.found) {
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(search.path, ec);
            std::cout << "  " << std::left << std::setw(8) << name << " " << search.path.string();
            if (!ec) {
                std::cout << "  (" << (bytes / (1024 * 1024)) << " MiB)";
            }
            std::cout << '\n';
            anyFound = true;
        } else {
            std::cout << "  " << std::left << std::setw(8) << name << " (not installed)\n";
        }
    }

    if (!anyFound) {
        std::cout << "\nNothing installed yet. Run: scripts\\download_model.ps1 -Model small\n";
    }
    return static_cast<int>(ExitCode::Ok);
}

#if defined(WHISPERFLOW_HAS_MICROPHONE)

// Background dictation: hold the hotkey, speak, release - the text lands in the
// window that has focus. The console only shows the log.
class DictationApp {
public:
    DictationApp(const whisperflow::AppConfig& config, whisperflow::Transcriber& transcriber,
                 whisperflow::PunctuationDictionary dictionary)
        : config_(config),
          transcriber_(transcriber),
          dictionary_(std::move(dictionary)),
          capture_([this](const std::vector<float>& chunk) { onAudioChunk(chunk); }) {}

    ~DictationApp() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    DictationApp(const DictationApp&) = delete;
    DictationApp& operator=(const DictationApp&) = delete;

    bool start() {
        hotkey_.setErrorHandler([](const std::string& message) { logLine("[Hotkey] " + message); });

        const auto parsed = whisperflow::parseHotkey(config_.hotkey);
        if (!parsed.spec) {
            logLine("[Hotkey] " + parsed.error);
            return false;
        }
        const auto combination = whisperflow::toCombination(*parsed.spec);
        if (!combination) {
            logLine("[Hotkey] cannot map '" + config_.hotkey + "' to a Windows key code");
            return false;
        }
        hotkeyCombination_ = *combination;

        // registerPushToTalk already reports the failure through the error handler,
        // so only the hint is added here - logging lastError() again would print twice.
        if (!hotkey_.registerPushToTalk(hotkeyCombination_, [this] { onHotkeyPressed(); },
                                        [this] { onHotkeyReleased(); })) {
            logLine("[Hotkey] another application holds that combination. Pick a different one:");
            logLine("[Hotkey]     WhisperFlowClone.exe --hotkey ctrl+alt+space");
            logLine("[Hotkey]     WhisperFlowClone.exe --hotkey f9");
            return false;
        }

        logLine("=== WhisperFlowClone - local offline dictation ===");
        logLine("Model:      " + transcriber_.options().modelPath.string());
        logLine("Language:   " + config_.language);
        logLine("Hotkey:     hold " + whisperflow::describeHotkey(hotkeyCombination_) +
                " to talk, release to paste");
        logLine("Status:     waiting. The text is inserted into the focused window.");
        logLine("Quit:       Ctrl+C in this console.");
        return true;
    }

    void run() {
        SetConsoleCtrlHandler(&DictationApp::consoleCtrlHandler, TRUE);
        runningInstance_ = this;
        hotkey_.runMessageLoop();
        runningInstance_ = nullptr;
        SetConsoleCtrlHandler(&DictationApp::consoleCtrlHandler, FALSE);
    }

    void stop() {
        hotkey_.stopMessageLoop();
    }

private:
    static BOOL WINAPI consoleCtrlHandler(DWORD controlType) {
        if (runningInstance_ != nullptr &&
            (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT ||
             controlType == CTRL_CLOSE_EVENT)) {
            runningInstance_->stop();
            return TRUE;
        }
        return FALSE;
    }

    void onAudioChunk(const std::vector<float>& chunk) {
        // Cheap append into a pre-reserved buffer: no per-callback allocation.
        pcm_.append(chunk);
    }

    void onHotkeyPressed() {
        if (!guard_.beginRecording()) {
            logLine("[Busy] " + guard_.busyMessage());
            return;
        }

        pcm_.clear();
        captureStart_ = Clock::now();
        capture_.startRecording();
        logLine("[Record] listening... release to transcribe");
    }

    void onHotkeyReleased() {
        capture_.stopRecording();
        const double captureMs = msSince(captureStart_);

        std::vector<float> buffer = pcm_.take();

        const whisperflow::SpeechVerdict verdict =
            whisperflow::evaluateSpeech(buffer, whisperflow::Transcriber::kSampleRate, gate_);

        if (!verdict.acceptable) {
            logLine("[Skip] " + verdict.reason + " - nothing inserted");
            guard_.finish();
            return;
        }

        if (!guard_.beginTranscribing()) {
            guard_.finish();
            return;
        }

        // The previous worker already finished (the guard was idle); reap it so
        // that assigning a new thread never hits a joinable std::thread.
        if (worker_.joinable()) {
            worker_.join();
        }
        worker_ = std::thread(&DictationApp::transcribeAndInject, this, std::move(buffer), captureMs);
    }

    void transcribeAndInject(std::vector<float> buffer, double captureMs) {
        const double originalSeconds =
            static_cast<double>(buffer.size()) / whisperflow::Transcriber::kSampleRate;

        // VAD: drop leading/trailing silence so the encoder never sees it.
        const auto vadStart = Clock::now();
        whisperflow::VadResult vad;
        if (config_.vad) {
            vad = whisperflow::detectSpeech(buffer, whisperflow::Transcriber::kSampleRate,
                                            vadSettings_);
            if (vad.hasSpeech) {
                buffer = std::vector<float>(
                    buffer.begin() + static_cast<std::ptrdiff_t>(vad.startSample),
                    buffer.begin() + static_cast<std::ptrdiff_t>(vad.endSample));
            }
        }
        const double vadTrimMs = msSince(vadStart);
        const double keptSeconds =
            static_cast<double>(buffer.size()) / whisperflow::Transcriber::kSampleRate;

        double injectMs = 0.0;
        transcriber_.transcribe(buffer, [&](const whisperflow::TranscriptionResult& result) {
            if (!result.ok) {
                logLine("[Error] " + result.error);
                return;
            }

            // Spoken punctuation + whitespace cleanup before anything is shown,
            // injected or stored in the phrase history.
            const std::string text = whisperflow::normalizeTranscript(result.text, dictionary_);
            if (!whisperflow::isMeaningfulText(text)) {
                logLine("[Skip] the recognizer heard no words - nothing inserted");
                return;
            }

            logLine("[Text] " + text);

            const auto injectStart = Clock::now();
            injector_.inject(text, [](const whisperflow::InjectionReport& report) {
                logLine(std::string(report.pasted ? "[Paste] " : "[Paste failed] ") + report.message);
            });
            injectMs = msSince(injectStart);

            // Full latency breakdown, as the prompt asks for.
            std::ostringstream timing;
            timing << std::fixed << std::setprecision(0);
            timing << "[Timing] capture " << captureMs << " ms"
                   << " | vad_trim " << vadTrimMs << " ms"
                   << " (kept " << std::setprecision(2) << keptSeconds << "/" << originalSeconds
                   << " s, cut " << std::setprecision(0)
                   << (vad.trimmedLeadingMs + vad.trimmedTrailingMs) << " ms)"
                   << " | encode " << result.encodeMs << " ms"
                   << " | decode " << result.decodeMs << " ms"
                   << " | inject " << injectMs << " ms"
                   << " | inference " << result.inferenceMs << " ms";
            if (result.audioSeconds > 0.0) {
                timing << std::setprecision(2) << " ("
                       << (result.inferenceMs / 1000.0) / result.audioSeconds << "x realtime)";
            }
            logLine(timing.str());
        });

        guard_.finish();
    }

    static DictationApp* runningInstance_;

    const whisperflow::AppConfig& config_;
    whisperflow::Transcriber& transcriber_;
    whisperflow::PunctuationDictionary dictionary_;
    whisperflow::AudioCapture capture_;
    whisperflow::HotkeyManager hotkey_;
    whisperflow::HotkeyCombination hotkeyCombination_{};  // resolved from config_.hotkey
    whisperflow::TextInjector injector_;
    whisperflow::SessionGuard guard_;
    whisperflow::SpeechGateSettings gate_;
    whisperflow::VadSettings vadSettings_;

    whisperflow::AudioBuffer pcm_;
    Clock::time_point captureStart_{};
    std::thread worker_;
};

DictationApp* DictationApp::runningInstance_ = nullptr;

int runInteractive(const whisperflow::AppConfig& config, whisperflow::Transcriber& transcriber,
                   const whisperflow::PunctuationDictionary& dictionary) {
    std::mutex pcmMutex;
    std::vector<float> pcm;
    std::size_t lastReported = 0;

    whisperflow::AudioCapture capture([&](const std::vector<float>& chunk) {
        std::lock_guard<std::mutex> lock(pcmMutex);
        pcm.insert(pcm.end(), chunk.begin(), chunk.end());

        const std::size_t seconds = pcm.size() / whisperflow::Transcriber::kSampleRate;
        if (seconds > lastReported) {
            lastReported = seconds;
            std::cout << "\r[Audio] recording... " << seconds << " s   " << std::flush;
        }
    });

    std::cout << "Press [Enter] to START recording...\n";
    std::cin.get();

    const auto start = Clock::now();
    capture.startRecording();
    std::cout << "Recording. Speak into your microphone.\nPress [Enter] to STOP and transcribe...\n";
    std::cin.get();
    capture.stopRecording();
    const double captureMs = msSince(start);

    std::vector<float> recorded;
    {
        std::lock_guard<std::mutex> lock(pcmMutex);
        recorded.swap(pcm);
    }

    std::cout << "\n";
    if (recorded.empty()) {
        std::cerr << "[Audio] No samples were captured. Check the default microphone "
                     "(Settings > System > Sound) and the app's microphone permission.\n";
        return static_cast<int>(ExitCode::CaptureFailed);
    }

    std::cout << "[Audio] captured " << recorded.size() << " samples ("
              << (static_cast<double>(recorded.size()) / whisperflow::Transcriber::kSampleRate)
              << " s)\n";

    if (!config.saveRecording.empty()) {
        std::string error;
        if (whisperflow::writeWavMono16k(config.saveRecording, recorded, error)) {
            std::cout << "[Audio] recording saved to " << config.saveRecording.string() << '\n';
        } else {
            std::cerr << "[Audio] " << error << '\n';
        }
    }

    return transcribeBuffer(std::move(recorded), captureMs, transcriber, config.language,
                            dictionary);
}

// Tray build: no console, settings.json is the source of truth, the hidden window
// also owns Shell_NotifyIcon and the hotkey message loop.
class TrayApp {
public:
    TrayApp(std::unique_ptr<whisperflow::Transcriber> transcriber, whisperflow::Settings settings,
            std::filesystem::path executableDirectory,
            whisperflow::PunctuationDictionary dictionary)
        : settings_(std::move(settings)),
          executableDirectory_(std::move(executableDirectory)),
          settingsPath_(whisperflow::settingsFilePath(executableDirectory_)),
          historyPath_(whisperflow::phraseHistoryFilePath(executableDirectory_)),
          dictionaryPath_(whisperflow::settingsFilePath(executableDirectory_).parent_path() /
                          "dictionary.json"),
          transcriber_(std::shared_ptr<whisperflow::Transcriber>(std::move(transcriber))),
          dictionary_(std::move(dictionary)),
          capture_([this](const std::vector<float>& chunk) { onAudioChunk(chunk); }) {}

    ~TrayApp() {
        if (modelWorker_.joinable()) {
            modelWorker_.join();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        tray_.destroy();
        overlay_.destroy();
    }

    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    void onAudioChunk(const std::vector<float>& chunk) {
        pcm_.append(chunk);
    }

    bool start() {
        history_.load(historyPath_);
        history_.setMaxEntries(settings_.maxHistoryEntries);

        hotkey_.setErrorHandler([this](const std::string& message) {
            setStatus("Hotkey error: " + message);
        });

        const auto parsed = whisperflow::parseHotkey(settings_.hotkey);
        if (!parsed.spec) {
            trayStatus_ = "Hotkey config error: " + parsed.error;
            return false;
        }
        const auto combination = whisperflow::toCombination(*parsed.spec);
        if (!combination) {
            trayStatus_ = "Hotkey config error";
            return false;
        }
        hotkeyCombination_ = *combination;

        if (!hotkey_.registerPushToTalk(
                hotkeyCombination_, [this] { onHotkeyPressed(); },
                [this] { onHotkeyReleased(); })) {
            trayStatus_ = "Hotkey is already registered by another app";
            return false;
        }

        hotkey_.setUserMessageHandler([this](UINT message, WPARAM wParam, LPARAM lParam) {
            return tray_.handleMessage(message, wParam, lParam);
        });

        if (!tray_.create(hotkey_.nativeHandle(), "WhisperFlowClone")) {
            trayStatus_ = "Could not create the tray icon";
            return false;
        }
        tray_.setCommandHandler([this](whisperflow::TrayCommand command) { onCommand(command); });
        tray_.setLanguage(settings_.language);
        tray_.setModel(settings_.modelSize);
        tray_.setAutostartEnabled(whisperflow::isAutostartEnabled());
        overlay_.create();

        tray_.setStatus(trayStatus_);
        return true;
    }

    void run() {
        hotkey_.runMessageLoop();
    }

    void stop() {
        hotkey_.stopMessageLoop();
    }

    // Why start() failed, in user-facing words (hotkey taken, tray icon refused...).
    [[nodiscard]] const std::string& startupError() const noexcept {
        return trayStatus_;
    }

private:
    void onCommand(whisperflow::TrayCommand command) {
        switch (command) {
            case whisperflow::TrayCommand::RepeatLast:
                repeatLast();
                break;
            case whisperflow::TrayCommand::LanguageAuto:
                switchLanguage("auto");
                break;
            case whisperflow::TrayCommand::LanguageRu:
                switchLanguage("ru");
                break;
            case whisperflow::TrayCommand::LanguageEn:
                switchLanguage("en");
                break;
            case whisperflow::TrayCommand::LanguageOther:
                setStatus("Set the language in settings.json");
                break;
            case whisperflow::TrayCommand::ModelTiny:
                switchModel("tiny");
                break;
            case whisperflow::TrayCommand::ModelBase:
                switchModel("base");
                break;
            case whisperflow::TrayCommand::ModelSmall:
                switchModel("small");
                break;
            case whisperflow::TrayCommand::ModelMedium:
                switchModel("medium");
                break;
            case whisperflow::TrayCommand::OpenModelsFolder:
                openModelsFolder();
                break;
            case whisperflow::TrayCommand::OpenSettingsFile:
                openSettingsFile();
                break;
            case whisperflow::TrayCommand::EditDictionary:
                editDictionary();
                break;
            case whisperflow::TrayCommand::ReloadDictionary:
                reloadDictionary();
                break;
            case whisperflow::TrayCommand::ToggleAutostart:
                toggleAutostart();
                break;
            case whisperflow::TrayCommand::Exit:
                stop();
                break;
        }
    }

    void onHotkeyPressed() {
        if (!guard_.beginRecording()) {
            setStatus("Busy: " + guard_.busyMessage());
            return;
        }
        if (!transcriber_ || !transcriber_->isLoaded()) {
            setStatus("Model is not loaded - open the models folder");
            tray_.showBalloon("Model", "Choose a model from the tray menu or download one first.");
            overlay_.hide();
            guard_.finish();
            return;
        }

        pcm_.clear();
        capture_.startRecording();
        overlay_.show("Listening...");
        setStatus("Recording...");
    }

    void onHotkeyReleased() {
        capture_.stopRecording();
        std::vector<float> buffer = pcm_.take();

        const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(
            buffer, whisperflow::Transcriber::kSampleRate, gate_);

        if (!verdict.acceptable) {
            overlay_.hide();
            setStatus("Skipped (" + verdict.reason + ")");
            guard_.finish();
            return;
        }

        if (!guard_.beginTranscribing()) {
            overlay_.hide();
            guard_.finish();
            return;
        }

        overlay_.show("Transcribing...");
        setStatus("Transcribing...");

        // The previous worker already finished (the guard was idle); reap it so
        // that assigning a new thread never hits a joinable std::thread.
        if (worker_.joinable()) {
            worker_.join();
        }
        worker_ = std::thread(&TrayApp::transcribeAndInject, this, std::move(buffer));
    }

    void transcribeAndInject(std::vector<float> buffer) {
        if (settings_.vad) {
            const whisperflow::VadResult vad = whisperflow::detectSpeech(
                buffer, whisperflow::Transcriber::kSampleRate, vadSettings_);
            if (vad.hasSpeech) {
                buffer = std::vector<float>(
                    buffer.begin() + static_cast<std::ptrdiff_t>(vad.startSample),
                    buffer.begin() + static_cast<std::ptrdiff_t>(vad.endSample));
            }
        }

        std::string message;
        bool ok = false;
        transcriber_->transcribe(buffer, [&](const whisperflow::TranscriptionResult& result) {
            if (!result.ok) {
                message = result.error;
                return;
            }

            // Spoken punctuation + whitespace cleanup before injection/history.
            const std::string text = whisperflow::normalizeTranscript(result.text, dictionary_);
            if (!whisperflow::isMeaningfulText(text)) {
                message = "the recognizer heard no words";
                return;
            }

            injector_.inject(text, [&](const whisperflow::InjectionReport& report) {
                if (!report.pasted) {
                    message = report.message;
                }
            });
            if (!message.empty()) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                history_.add(text);
                std::string historyError;
                if (!history_.save(historyError) && !historyError.empty()) {
                    message = historyError;
                }
            }
            ok = true;
        });

        hotkey_.post([this, ok, message] {
            guard_.finish();
            overlay_.hide();
            if (ok) {
                setStatus("Inserted");
            } else {
                setStatus("Error: " + message);
            }
        });
    }

    void repeatLast() {
        if (!guard_.isIdle()) {
            setStatus("Busy");
            return;
        }
        std::string phrase;
        {
            std::lock_guard<std::mutex> lock(historyMutex_);
            phrase = history_.last();
        }
        if (phrase.empty()) {
            setStatus("Phrase history is empty");
            tray_.showBalloon("Phrase history", "There is no phrase to repeat yet.");
            return;
        }

        injector_.inject(phrase, [this](const whisperflow::InjectionReport& report) {
            if (report.pasted) {
                setStatus("Repeated last phrase");
            } else {
                setStatus("Repeat failed: " + report.message);
            }
        });
    }

    void switchLanguage(const std::string& language) {
        if (language == settings_.language) {
            return;
        }
        settings_.language = language;
        saveCurrentSettings();
        if (transcriber_) {
            transcriber_->setLanguage(language);
        }
        tray_.setLanguage(language);
        setStatus("Language: " + language);
    }

    void switchModel(const std::string& size) {
        if (!guard_.isIdle()) {
            setStatus("Busy - cannot switch model now");
            return;
        }
        if (modelWorker_.joinable() || loadingModel_) {
            setStatus("Model is already loading");
            return;
        }
        if (size == settings_.modelSize && transcriber_ && transcriber_->isLoaded()) {
            return;
        }

        loadingModel_ = true;
        guard_.beginRecording();  // keep the hotkey path out while the model swaps
        settings_.modelSize = size;
        saveCurrentSettings();
        tray_.setModel(size);
        overlay_.show("Loading " + size + "...");
        setStatus("Loading " + size + "...");

        // Snapshot the settings on the UI thread. The worker only reads these
        // locals so a concurrent menu action cannot race on settings_.
        const std::string language = settings_.language;
        const std::string initialPrompt = settings_.initialPrompt;
        const int threads = settings_.threads;
        const bool useGpu = settings_.useGpu;
        const bool translate = settings_.translateToEnglish;
        const bool shrinkContext = settings_.shrinkContext;

        modelWorker_ = std::thread([this, size, language, initialPrompt, threads, useGpu, translate,
                                    shrinkContext] {
            whisperflow::ModelSize parsed{};
            if (!whisperflow::parseModelSize(size, parsed)) {
                postModelReady(size, nullptr, "unknown model size");
                return;
            }

            whisperflow::ModelQuery query;
            query.size = parsed;
            query.executableDirectory = executableDirectory_;
            const whisperflow::ModelSearch search = whisperflow::locateModel(query);
            if (!search.found) {
                const std::string message = "model " + size + " is not installed";
                postModelReady(size, nullptr, message);
                return;
            }

            whisperflow::TranscriptionOptions options;
            options.modelPath = search.path;
            options.language = language;
            options.initialPrompt = initialPrompt;
            options.threads = threads;
            options.useGpu = useGpu;
            options.translateToEnglish = translate;
            options.shrinkContextForShortAudio = shrinkContext;

            auto next = std::make_shared<whisperflow::Transcriber>(options);
            const bool loaded = next->ensureLoaded();
            const std::string loadError = loaded ? std::string() : next->lastError();
            postModelReady(size, next, loadError);
        });
    }

    void postModelReady(const std::string& size, std::shared_ptr<whisperflow::Transcriber> next,
                        const std::string& error) {
        hotkey_.post([this, size, next = std::move(next), error] {
            if (modelWorker_.joinable()) {
                modelWorker_.join();
            }
            loadingModel_ = false;
            guard_.finish();
            overlay_.hide();
            if (!error.empty()) {
                setStatus("Error: " + error);
                tray_.showBalloon("Model", "Could not load " + size + ": " + error);
                return;
            }
            transcriber_ = next;
            tray_.setModel(size);
            tray_.setLanguage(settings_.language);
            setStatus("Model " + size + " ready");
            tray_.showBalloon("Model", "Loaded " + size);
        });
    }

    void toggleAutostart() {
        const bool enabled = !whisperflow::isAutostartEnabled();
        std::string error;
        if (!whisperflow::setAutostartEnabled(enabled, error)) {
            setStatus("Autostart error: " + error);
            return;
        }
        settings_.startWithWindows = enabled;
        saveCurrentSettings();
        tray_.setAutostartEnabled(enabled);
        setStatus(enabled ? "Autostart enabled" : "Autostart disabled");
    }

    void openModelsFolder() const {
        const std::filesystem::path dir = whisperflow::userModelsDirectory();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void openSettingsFile() {
        std::error_code existsError;
        if (!std::filesystem::exists(settingsPath_, existsError)) {
            std::string error;
            const bool saved = whisperflow::saveSettings(settingsPath_, settings_, error);
            (void)saved;
        }
        ShellExecuteW(nullptr, L"open", settingsPath_.wstring().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }

    // Opens dictionary.json in the user's editor, creating it from the current
    // (built-in or loaded) dictionary first so there is always something to edit.
    void editDictionary() {
        std::error_code ec;
        if (!std::filesystem::exists(dictionaryPath_, ec)) {
            std::string error;
            if (!whisperflow::writePunctuationDictionary(dictionaryPath_, dictionary_, error)) {
                setStatus("Dictionary error: " + error);
                return;
            }
        }
        ShellExecuteW(nullptr, L"open", dictionaryPath_.wstring().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        setStatus("Edit dictionary.json, then choose Reload dictionary");
    }

    // Re-reads the dictionary without restarting the app. A broken file keeps
    // the dictionary that is currently in use: dictation must never break.
    void reloadDictionary() {
        whisperflow::PunctuationDictionary reloaded;
        if (!whisperflow::readPunctuationDictionary(dictionaryPath_, reloaded) ||
            reloaded.empty()) {
            const std::filesystem::path portable = executableDirectory_ / "dictionary.json";
            if (!whisperflow::readPunctuationDictionary(portable, reloaded) || reloaded.empty()) {
                setStatus("Dictionary is missing or invalid - keeping the previous one");
                tray_.showBalloon("Dictionary", "dictionary.json could not be parsed.");
                return;
            }
        }
        dictionary_ = std::move(reloaded);
        setStatus("Dictionary reloaded (" + std::to_string(dictionary_.size()) + " entries)");
    }

    void saveCurrentSettings() {
        std::string error;
        const bool saved = whisperflow::saveSettings(settingsPath_, settings_, error);
        if (!saved || !error.empty()) {
            setStatus("Settings save error: " + error);
        }
    }

    void setStatus(const std::string& text) {
        trayStatus_ = text;
        tray_.setStatus(text);
    }

    whisperflow::Settings settings_;
    std::filesystem::path executableDirectory_;
    std::filesystem::path settingsPath_;
    std::filesystem::path historyPath_;
    std::filesystem::path dictionaryPath_;
    std::shared_ptr<whisperflow::Transcriber> transcriber_;
    whisperflow::PunctuationDictionary dictionary_;
    whisperflow::AudioCapture capture_;
    whisperflow::HotkeyManager hotkey_;
    whisperflow::HotkeyCombination hotkeyCombination_{};
    whisperflow::TextInjector injector_;
    whisperflow::SessionGuard guard_;
    whisperflow::SpeechGateSettings gate_;
    whisperflow::VadSettings vadSettings_;
    whisperflow::Overlay overlay_;
    whisperflow::TrayIcon tray_;
    whisperflow::PhraseHistory history_;
    std::mutex historyMutex_;

    whisperflow::AudioBuffer pcm_;
    std::thread worker_;
    std::thread modelWorker_;
    bool loadingModel_{false};
    std::string trayStatus_{"Waiting for hotkey..."};
};

#endif  // WHISPERFLOW_HAS_MICROPHONE

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    whisperflow::AppConfig config = whisperflow::loadConfig(argc, argv);
    if (!config.valid) {
        std::cerr << config.error << "\n\n" << whisperflow::usageText();
        return static_cast<int>(ExitCode::BadArguments);
    }
    if (config.showHelp) {
        std::cout << whisperflow::usageText();
        return static_cast<int>(ExitCode::Ok);
    }

    whisperflow::ModelQuery query;
    query.size = config.modelSize;
    query.explicitPath = config.modelPath;
    query.executableDirectory = executableDirectory();

    if (config.listModels) {
        return listInstalledModels(query);
    }

    const std::filesystem::path exeDir = executableDirectory();
    whisperflow::Settings settings = whisperflow::loadSettings(
        whisperflow::settingsFilePath(exeDir));

    // --tray treats settings.json as the source of truth; the other mode keeps
    // the legacy config.ini + CLI behaviour unchanged.
    if (config.trayMode) {
        whisperflow::applySettingsToConfig(settings, config);
        // Rebuild the model query because --tray may have changed the size/hotkey
        // from settings.json after the query was initialized above.
        query.size = config.modelSize;
        query.explicitPath = config.modelPath;
        query.executableDirectory = exeDir;
    }

    std::cout << "=== WhisperFlowClone - local offline speech-to-text ===\n";
    std::cout << "whisper.cpp " << whisperflow::Transcriber::version() << " | "
              << whisperflow::Transcriber::backendInfo() << '\n';

    const whisperflow::ModelSearch search = whisperflow::locateModel(query);
    if (!search.found && !config.trayMode) {
        std::cerr << '\n' << whisperflow::describeMissingModel(search) << '\n';
        return static_cast<int>(ExitCode::ModelMissing);
    }
    if (search.found) {
        std::cout << "Model:      " << search.path.string() << '\n';
    } else if (config.trayMode) {
        std::cout << "Model:      (not installed - the tray app will keep running)\n";
    }
    std::cout << "Language:   " << config.language << '\n';

    if (!whisperflow::Transcriber::isKnownLanguage(config.language)) {
        std::cerr << "[Config] Unknown language code '" << config.language
                  << "'. Use 'auto' or a Whisper language code such as 'ru' or 'en'.\n";
        return static_cast<int>(ExitCode::BadArguments);
    }

    // Spoken-punctuation dictionary: <exe dir>/dictionary.json first, then next
    // to settings.json; built-in defaults when neither exists or parses.
    const whisperflow::PunctuationDictionary dictionary =
        whisperflow::loadPunctuationDictionary({exeDir / "dictionary.json",
            whisperflow::settingsFilePath(exeDir).parent_path() / "dictionary.json"});

    whisperflow::TranscriptionOptions options;
    options.modelPath = search.found ? search.path : std::filesystem::path();
    options.language = config.language;
    options.initialPrompt = config.initialPrompt;
    options.threads = config.threads;
    options.useGpu = config.useGpu;
    options.translateToEnglish = config.translateToEnglish;
    options.shrinkContextForShortAudio = config.shrinkContext;

    auto transcriber = std::make_unique<whisperflow::Transcriber>(options);
    transcriber->setLogHandler([](const std::string& line) {
        std::cerr << "[whisper] " << line << '\n';
    });

    const auto loadStart = Clock::now();
    if (search.found && !transcriber->ensureLoaded()) {
        std::cerr << "[Transcriber] " << transcriber->lastError() << '\n';
        return static_cast<int>(ExitCode::TranscriptionFailed);
    }
    if (search.found) {
        std::cout << "Model load: " << static_cast<long long>(msSince(loadStart)) << " ms (warm)\n";
    }

#if defined(WHISPERFLOW_HAS_MICROPHONE)
    if (config.trayMode) {
        if (!config.wavInput.empty() || config.interactive) {
            std::cerr << "[Config] --tray cannot be combined with --wav or --interactive.\n";
            return static_cast<int>(ExitCode::BadArguments);
        }
        TrayApp app(std::move(transcriber), std::move(settings), exeDir, dictionary);
        if (!app.start()) {
            // No console in tray mode: a message box is the only feedback channel.
            const std::string message = "WhisperFlowClone could not start: " + app.startupError() +
                                        "\n\nEdit settings.json (hotkey) and try again.";
            std::cerr << "[Tray] " << message << '\n';
            const int wide = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
            std::wstring wmessage(static_cast<std::size_t>(wide > 0 ? wide : 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wmessage.data(), wide);
            MessageBoxW(nullptr, wmessage.c_str(), L"WhisperFlowClone", MB_OK | MB_ICONERROR);
            return static_cast<int>(ExitCode::HotkeyFailed);
        }
        app.run();
        return static_cast<int>(ExitCode::Ok);
    }

    if (!config.wavInput.empty()) {
        return runFromWavFile(config, *transcriber, dictionary);
    }
    if (config.interactive) {
        return runInteractive(config, *transcriber, dictionary);
    }

    DictationApp app(config, *transcriber, dictionary);
    if (!app.start()) {
        return static_cast<int>(ExitCode::HotkeyFailed);
    }
    app.run();
    return static_cast<int>(ExitCode::Ok);
#else
    if (config.trayMode) {
        std::cerr << "[Config] --tray is only available in the Windows build.\n";
        return static_cast<int>(ExitCode::BadArguments);
    }
    if (config.wavInput.empty()) {
        std::cerr << "This build has no microphone capture (non-Windows). Use --wav <file>.\n";
        return static_cast<int>(ExitCode::CaptureFailed);
    }
    return runFromWavFile(config, *transcriber, dictionary);
#endif
}

#if defined(_WIN32) && defined(WHISPERFLOW_HAS_MICROPHONE)

namespace {

// The executable is a Windows-subsystem binary (no console of its own) so that
// --tray and autostart never flash a console window. When it is launched from
// cmd.exe / PowerShell, reattach to that console so --help, --list-models,
// --interactive and the log lines are visible. Returns false when there is no
// parent console (double-click, autostart, Task Scheduler).
bool attachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return false;
    }
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    (void)freopen_s(&stream, "CONIN$", "r", stdin);
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();
    return true;
}

}  // namespace

// CMake builds this target as a Windows-subsystem executable, so the CRT calls
// WinMain instead of main. Rebuild the UTF-8 argv and delegate to main() so the
// rest of the application is not duplicated.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const bool hasConsole = attachParentConsole();

    int argc = 0;
    LPWSTR* rawArgs = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (rawArgs == nullptr) {
        return static_cast<int>(ExitCode::CaptureFailed);
    }
    std::vector<std::string> utf8Args;
    std::vector<char*> argv;
    utf8Args.reserve(static_cast<std::size_t>(argc) + 1);
    argv.reserve(static_cast<std::size_t>(argc) + 1);
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, rawArgs[i], -1, nullptr, 0, nullptr,
                                               nullptr);
        std::string arg;
        if (length > 1) {
            arg.resize(static_cast<std::size_t>(length - 1));
            WideCharToMultiByte(CP_UTF8, 0, rawArgs[i], -1, arg.data(), length, nullptr, nullptr);
        }
        utf8Args.push_back(std::move(arg));
    }
    LocalFree(rawArgs);

    // Double-click / autostart without arguments: there is no console to log to,
    // so the tray UI is the only mode that gives the user any feedback or a way
    // to exit. Explicit arguments are always respected as typed.
    if (!hasConsole && argc == 1) {
        utf8Args.emplace_back("--tray");
    }
    for (std::string& arg : utf8Args) {
        argv.push_back(arg.data());
    }
    return main(static_cast<int>(argv.size()), argv.data());
}

#endif  // _WIN32 && WHISPERFLOW_HAS_MICROPHONE
