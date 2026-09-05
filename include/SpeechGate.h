#pragma once

#include <string>
#include <vector>

namespace whisperflow {

// Thresholds used to decide whether a captured buffer is worth transcribing.
// Values are conservative: they drop room silence and accidental key taps,
// but keep short words.
struct SpeechGateSettings {
    double minSeconds{0.40};   // shorter than this - a click, not speech
    float minPeak{0.010f};     // ~ -40 dBFS
    float minRms{0.0020f};     // ~ -54 dBFS
};

struct SpeechVerdict {
    bool acceptable{false};
    std::string reason;  // empty when acceptable
    double seconds{0.0};
    float peak{0.0f};
    float rms{0.0f};
};

// Analyses PCM (mono, 16 kHz, float in [-1, 1]) and decides whether to run
// inference at all. Pure function, no I/O - unit tested.
[[nodiscard]] SpeechVerdict evaluateSpeech(const std::vector<float>& pcmMono16k, int sampleRate,
                                           const SpeechGateSettings& settings = {});

// Whisper answers silence with "." / "..." / music notes. Such output must not
// be pasted into the user's document. Pure function - unit tested.
[[nodiscard]] bool isMeaningfulText(const std::string& text);

}  // namespace whisperflow
