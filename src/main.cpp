#include "AppConfig.h"
#include "ModelLocator.h"
#include "Transcriber.h"
#include "WavFile.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#if defined(WHISPERFLOW_HAS_MICROPHONE)
#include "AudioCapture.h"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

enum class ExitCode {
    Ok = 0,
    BadArguments = 2,
    ModelMissing = 3,
    TranscriptionFailed = 4,
    CaptureFailed = 5,
};

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
    std::cout << "Inference:  " << result.inferenceMs << " ms\n";
    if (result.audioSeconds > 0.0) {
        std::cout << std::setprecision(2);
        std::cout << "Real-time:  " << (result.inferenceMs / 1000.0) / result.audioSeconds
                  << "x  (inference time / audio time, lower is better)\n";
    }
    std::cout << std::setprecision(0);
}

int transcribeBuffer(std::vector<float> pcm, double captureMs, whisperflow::Transcriber& transcriber,
                     const std::string& languageHint) {
    int exitCode = static_cast<int>(ExitCode::Ok);
    std::string failure;

    transcriber.transcribe(pcm, [&](const whisperflow::TranscriptionResult& result) {
        if (!result.ok) {
            failure = result.error;
            exitCode = static_cast<int>(ExitCode::TranscriptionFailed);
            return;
        }

        std::cout << "\n--- Recognized text ---\n" << result.text << "\n";
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

int runFromWavFile(const whisperflow::AppConfig& config, whisperflow::Transcriber& transcriber) {
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

    return transcribeBuffer(std::move(audio.samples), readMs, transcriber, config.language);
}

#if defined(WHISPERFLOW_HAS_MICROPHONE)
int runFromMicrophone(const whisperflow::AppConfig& config, whisperflow::Transcriber& transcriber) {
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

    // stopRecording() joined the capture thread, so pcm is no longer mutated.
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

    return transcribeBuffer(std::move(recorded), captureMs, transcriber, config.language);
}
#endif

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
        std::cout << "\nNothing installed yet. Run: scripts\\download-model.ps1 -Model small\n";
    }
    return static_cast<int>(ExitCode::Ok);
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    const whisperflow::AppConfig config = whisperflow::loadConfig(argc, argv);
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

    std::cout << "=== WhisperFlowClone - local offline speech-to-text ===\n";
    std::cout << "whisper.cpp " << whisperflow::Transcriber::version() << " | "
              << whisperflow::Transcriber::backendInfo() << '\n';

    const whisperflow::ModelSearch search = whisperflow::locateModel(query);
    if (!search.found) {
        std::cerr << '\n' << whisperflow::describeMissingModel(search) << '\n';
        return static_cast<int>(ExitCode::ModelMissing);
    }
    std::cout << "Model:      " << search.path.string() << '\n';
    std::cout << "Language:   " << config.language << '\n';

    if (!whisperflow::Transcriber::isKnownLanguage(config.language)) {
        std::cerr << "[Config] Unknown language code '" << config.language
                  << "'. Use 'auto' or a Whisper language code such as 'ru' or 'en'.\n";
        return static_cast<int>(ExitCode::BadArguments);
    }

    whisperflow::TranscriptionOptions options;
    options.modelPath = search.path;
    options.language = config.language;
    options.threads = config.threads;
    options.useGpu = config.useGpu;
    options.translateToEnglish = config.translateToEnglish;

    whisperflow::Transcriber transcriber(options);
    transcriber.setLogHandler([](const std::string& line) {
        std::cerr << "[whisper] " << line << '\n';
    });

    const auto loadStart = Clock::now();
    if (!transcriber.ensureLoaded()) {
        std::cerr << "[Transcriber] " << transcriber.lastError() << '\n';
        return static_cast<int>(ExitCode::TranscriptionFailed);
    }
    std::cout << "Model load: " << static_cast<long long>(msSince(loadStart)) << " ms (warm)\n";

#if defined(WHISPERFLOW_HAS_MICROPHONE)
    if (!config.wavInput.empty()) {
        return runFromWavFile(config, transcriber);
    }
    return runFromMicrophone(config, transcriber);
#else
    if (config.wavInput.empty()) {
        std::cerr << "This build has no microphone capture (non-Windows). Use --wav <file>.\n";
        return static_cast<int>(ExitCode::CaptureFailed);
    }
    return runFromWavFile(config, transcriber);
#endif
}
