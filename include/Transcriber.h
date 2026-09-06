#pragma once

#include <cstddef>
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
    // Shrink the encoder's audio context to fit the (already VAD-trimmed) clip.
    // The whisper encoder normally always processes a full 30 s mel window; for a
    // 2-5 s dictation that is wasted work. Reducing audio_ctx to the real length
    // is the single biggest CPU latency win for short utterances. On by default.
    bool shrinkContextForShortAudio{true};
};

// Options after defaults have been applied. Pure data, safe to assert on.
struct ResolvedParams {
    int threads{4};
    bool detectLanguage{true};
    std::string language;  // used only when detectLanguage == false
    bool translate{false};
    bool singleSegment{false};
    bool useGpu{true};
    bool shrinkContextForShortAudio{true};
};

// Whisper's mel window is 30 s and the full audio context is 1500 frames. For a
// clip shorter than 30 s we only need enough frames to cover it (2 mel frames per
// 20 ms hop), rounded up with slack. Returns 0 to mean "use the default context".
// Exposed for unit testing.
[[nodiscard]] int audioContextForSamples(std::size_t numSamples, int sampleRate);

struct TranscriptionResult {
    bool ok{false};
    std::string text;
    std::string error;
    double modelLoadMs{0.0};   // 0 when the model was already warm
    double inferenceMs{0.0};   // wall-clock time of whisper_full()
    double encodeMs{0.0};      // encoder time reported by whisper.cpp
    double decodeMs{0.0};      // decoder (+ sampling) time reported by whisper.cpp
    double audioSeconds{0.0};  // duration actually fed to the model (after VAD trim)
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

    // Changes the language for the next transcribe() call. The loaded model is
    // kept warm; only the per-utterance whisper language is updated.
    void setLanguage(const std::string& language);

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
