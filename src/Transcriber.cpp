#include "Transcriber.h"

#include <whisper.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace whisperflow {
namespace {

constexpr int kMaxThreads = 16;

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    });
    return text;
}

std::string trim(const std::string& text) {
    const char* const kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

// whisper.cpp expects UTF-8 paths on Windows (it converts them to UTF-16 itself),
// so Cyrillic user profiles keep working.
std::string toUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

void routeWhisperLog(ggml_log_level level, const char* text, void* userData) {
    auto* handler = static_cast<Transcriber::LogHandler*>(userData);
    if (handler == nullptr || text == nullptr || !*handler) {
        return;
    }
    if (level == GGML_LOG_LEVEL_DEBUG) {
        return;
    }
    std::string line(text);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (!line.empty()) {
        (*handler)(line);
    }
}

}  // namespace

ResolvedParams resolveParams(const TranscriptionOptions& options) {
    ResolvedParams resolved;

    int hardware = static_cast<int>(std::thread::hardware_concurrency());
    if (hardware <= 0) {
        hardware = 4;
    }
    hardware = std::min(hardware, kMaxThreads);
    resolved.threads = (options.threads > 0) ? options.threads : hardware;

    const std::string language = toLower(trim(options.language));
    resolved.detectLanguage = language.empty() || language == "auto";
    resolved.language = resolved.detectLanguage ? std::string() : language;
    resolved.translate = options.translateToEnglish;
    resolved.singleSegment = options.singleSegment;
    resolved.useGpu = options.useGpu;
    resolved.shrinkContextForShortAudio = options.shrinkContextForShortAudio;
    return resolved;
}

int audioContextForSamples(std::size_t numSamples, int sampleRate) {
    constexpr int kFullAudioCtx = 1500;   // encoder frames for the full 30 s window
    constexpr int kHopLength = 160;       // WHISPER_HOP_LENGTH: samples per mel frame
    constexpr int kFramesPerCtx = 2;      // the encoder downsamples mel by 2

    const int rate = (sampleRate > 0) ? sampleRate : 16000;
    if (numSamples == 0) {
        return 0;
    }

    // Mel frames needed to cover the audio, then encoder context frames, + slack.
    const double seconds = static_cast<double>(numSamples) / static_cast<double>(rate);
    const double melFrames = seconds * static_cast<double>(rate) / static_cast<double>(kHopLength);
    int ctx = static_cast<int>(melFrames / kFramesPerCtx) + 16;  // round up + headroom

    if (ctx >= kFullAudioCtx) {
        return 0;  // audio is >= 30 s: keep the default, do not clamp
    }
    if (ctx < 32) {
        ctx = 32;  // a sane floor so very short clips still decode
    }
    return ctx;
}

class Transcriber::Impl {
public:
    explicit Impl(TranscriptionOptions options)
        : options_(std::move(options)) {}

    ~Impl() {
        if (logInstalled_) {
            whisper_log_set(nullptr, nullptr);
            logInstalled_ = false;
        }
        if (context_ != nullptr) {
            whisper_free(context_);
            context_ = nullptr;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void setLogHandler(LogHandler handler) {
        logHandler_ = std::move(handler);
        whisper_log_set(&routeWhisperLog, logHandler_ ? &logHandler_ : nullptr);
        logInstalled_ = true;
    }

    void setLanguage(const std::string& language) {
        const std::string normalized = toLower(trim(language));
        if (!normalized.empty() && normalized != "auto" &&
            whisper_lang_id(normalized.c_str()) < 0) {
            lastError_ = "Unknown language code '" + language + "'.";
            return;
        }
        lastError_.clear();
        options_.language = normalized;
    }

    bool ensureLoaded() {
        if (context_ != nullptr) {
            return true;
        }
        if (options_.modelPath.empty()) {
            lastError_ = "No model path configured.";
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(options_.modelPath, ec)) {
            lastError_ = "Model file not found: " + options_.modelPath.string();
            return false;
        }

        const auto start = Clock::now();

        whisper_context_params contextParams = whisper_context_default_params();
        contextParams.use_gpu = options_.useGpu;

        const std::string utf8Path = toUtf8(options_.modelPath);
        context_ = whisper_init_from_file_with_params(utf8Path.c_str(), contextParams);
        modelLoadMs_ = msSince(start);

        if (context_ == nullptr) {
            lastError_ = "whisper.cpp could not load the model: " + options_.modelPath.string();
            modelLoadMs_ = 0.0;
            return false;
        }
        return true;
    }

    TranscriptionResult run(const std::vector<float>& pcmMono16k) {
        TranscriptionResult result;
        result.modelPath = options_.modelPath.string();

        if (pcmMono16k.empty()) {
            result.error = "Nothing was recorded: the audio buffer is empty.";
            return result;
        }

        if (!ensureLoaded()) {
            result.error = lastError_;
            return result;
        }
        if (!modelLoadReported_) {
            result.modelLoadMs = modelLoadMs_;
            modelLoadReported_ = true;
        }

        const ResolvedParams resolved = resolveParams(options_);

        std::string language = resolved.language;
        const bool multilingual = (context_ != nullptr) && (whisper_is_multilingual(context_) != 0);
        if (context_ != nullptr && !multilingual) {
            // English-only models (.en) reject any other language code.
            language = "en";
        }
        if (!language.empty() && whisper_lang_id(language.c_str()) < 0) {
            result.error = "Unknown language code '" + language + "'.";
            return result;
        }

        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.n_threads = resolved.threads;
        params.translate = resolved.translate;
        params.no_context = true;        // one-shot dictation: do not carry a prompt over
        params.single_segment = resolved.singleSegment;
        params.print_progress = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.print_special = false;
        params.language = language.empty() ? "auto" : language.c_str();
        params.detect_language = false;

        // For a short clip, shrink the encoder's audio context so it does not
        // process the full 30 s mel window. This is the main latency win for
        // 2-5 s dictation. audio_ctx == 0 keeps whisper's default (full window).
        if (resolved.shrinkContextForShortAudio) {
            params.audio_ctx = audioContextForSamples(pcmMono16k.size(), kSampleRate);
        }

        // whisper accumulates timings in the context; reset so encode/decode
        // reflect only this utterance.
        whisper_reset_timings(context_);

        const auto start = Clock::now();
        const int status = whisper_full(context_, params, pcmMono16k.data(),
                                        static_cast<int>(pcmMono16k.size()));
        result.inferenceMs = msSince(start);

        if (const whisper_timings* timings = whisper_get_timings(context_)) {
            result.encodeMs = static_cast<double>(timings->encode_ms);
            result.decodeMs = static_cast<double>(timings->decode_ms) +
                              static_cast<double>(timings->sample_ms) +
                              static_cast<double>(timings->batchd_ms) +
                              static_cast<double>(timings->prompt_ms);
        }

        if (status != 0) {
            result.error = "whisper_full failed with status " + std::to_string(status) + ".";
            return result;
        }

        const int segments = whisper_full_n_segments(context_);
        std::string text;
        for (int i = 0; i < segments; ++i) {
            const char* piece = whisper_full_get_segment_text(context_, i);
            if (piece != nullptr) {
                text += piece;
            }
        }

        const int langId = whisper_full_lang_id(context_);
        if (const char* langName = whisper_lang_str(langId)) {
            result.language = langName;
        }

        result.text = trim(text);
        result.audioSeconds = static_cast<double>(pcmMono16k.size()) / static_cast<double>(kSampleRate);
        result.ok = true;
        return result;
    }

    bool isLoaded() const noexcept { return context_ != nullptr; }
    const std::string& lastError() const noexcept { return lastError_; }
    double modelLoadMs() const noexcept { return modelLoadMs_; }
    const TranscriptionOptions& options() const noexcept { return options_; }

private:
    TranscriptionOptions options_;
    LogHandler logHandler_;
    whisper_context* context_{nullptr};
    std::string lastError_;
    double modelLoadMs_{0.0};
    bool modelLoadReported_{false};
    bool logInstalled_{false};
};

Transcriber::Transcriber(TranscriptionOptions options)
    : pImpl_(std::make_unique<Impl>(std::move(options))) {}

Transcriber::~Transcriber() = default;
Transcriber::Transcriber(Transcriber&&) noexcept = default;
Transcriber& Transcriber::operator=(Transcriber&&) noexcept = default;

void Transcriber::setLogHandler(LogHandler handler) {
    pImpl_->setLogHandler(std::move(handler));
}

void Transcriber::setLanguage(const std::string& language) {
    pImpl_->setLanguage(language);
}

bool Transcriber::ensureLoaded() {
    return pImpl_->ensureLoaded();
}

bool Transcriber::isLoaded() const noexcept {
    return pImpl_->isLoaded();
}

const std::string& Transcriber::lastError() const noexcept {
    return pImpl_->lastError();
}

double Transcriber::modelLoadMs() const noexcept {
    return pImpl_->modelLoadMs();
}

const TranscriptionOptions& Transcriber::options() const noexcept {
    return pImpl_->options();
}

void Transcriber::transcribe(const std::vector<float>& pcmMono16k, const ResultHandler& onResult) {
    TranscriptionResult result = pImpl_->run(pcmMono16k);
    if (onResult) {
        onResult(result);
    }
}

std::string Transcriber::backendInfo() {
    const char* info = whisper_print_system_info();
    return info != nullptr ? std::string(info) : std::string();
}

std::string Transcriber::version() {
    const char* version = whisper_version();
    return version != nullptr ? std::string(version) : std::string();
}

bool Transcriber::isKnownLanguage(const std::string& language) {
    const std::string normalized = toLower(trim(language));
    if (normalized.empty() || normalized == "auto") {
        return true;
    }
    return whisper_lang_id(normalized.c_str()) >= 0;
}

}  // namespace whisperflow
