#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace whisperflow {

// What the recognizer should do. Kept free of whisper.cpp types so that this
// header stays cheap to include and unit-testable.
struct TranscriptionOptions {
    std::filesystem::path modelPath;
    std::string language{"auto"};  // "auto" = detect, otherwise "ru", "en", ...
    int threads{0};                // 0 = all logical cores (capped)
    bool useGpu{true};             // ignored unless a GPU backend was compiled in
    bool translateToEnglish{false};
    bool singleSegment{false};
};

// Options after defaults have been applied. Pure data, safe to assert on.
struct ResolvedParams {
    int threads{4};
    bool detectLanguage{true};
    std::string language;  // used only when detectLanguage == false
    bool translate{false};
    bool singleSegment{false};
    bool useGpu{true};
};

struct TranscriptionResult {
    bool ok{false};
    std::string text;
    std::string error;
    double modelLoadMs{0.0};   // 0 when the model was already warm
    double inferenceMs{0.0};
    double audioSeconds{0.0};
    std::string modelPath;
    std::string language;      // language actually used for this utterance
};

[[nodiscard]] ResolvedParams resolveParams(const TranscriptionOptions& options);

// Local, offline speech-to-text on top of whisper.cpp (MIT).
// The result is delivered through a std::function, not a virtual interface.
class Transcriber {
public:
    using ResultHandler = std::function<void(const TranscriptionResult& result)>;
    using LogHandler = std::function<void(const std::string& line)>;

    static constexpr int kSampleRate = 16000;

    explicit Transcriber(TranscriptionOptions options);
    ~Transcriber();

    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;
    Transcriber(Transcriber&&) noexcept;
    Transcriber& operator=(Transcriber&&) noexcept;

    // Routes whisper.cpp's own log output (model loading, timings) to a handler.
    void setLogHandler(LogHandler handler);

    // Loads the model into memory. Call once at startup so that the first
    // dictation does not pay the load cost. Safe to call more than once.
    bool ensureLoaded();

    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;
    [[nodiscard]] double modelLoadMs() const noexcept;
    [[nodiscard]] const TranscriptionOptions& options() const noexcept;

    // Input: accumulated PCM, mono, 16 kHz, float in [-1, 1].
    // onResult is invoked exactly once, on the calling thread.
    void transcribe(const std::vector<float>& pcmMono16k, const ResultHandler& onResult);

    // CPU features the linked ggml build can use (AVX/AVX2/NEON/...).
    [[nodiscard]] static std::string backendInfo();
    [[nodiscard]] static std::string version();
    [[nodiscard]] static bool isKnownLanguage(const std::string& language);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace whisperflow
