#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace whisperflow {

// Energy-based Voice Activity Detection.
//
// Deliberately dependency-free: it is our own MIT code, so it adds no third-party
// license to justify. whisper.cpp v1.9.3 ships a Silero VAD, but it requires a
// separate VAD model file to be downloaded and shipped, which is extra weight and
// an extra license surface - not worth it for trimming leading/trailing silence.
//
// The detector splits the buffer into short frames, measures each frame's RMS,
// and keeps the span from the first to the last frame that is loud enough to be
// speech (plus a little padding for context). A relative threshold (a fraction of
// the loudest frame) makes it adapt to the speaker's volume; an absolute floor
// keeps room hiss from ever counting as speech.
struct VadSettings {
    double frameMs{20.0};          // analysis frame length
    double paddingMs{120.0};       // context kept on each side of detected speech
    float absoluteRmsFloor{0.006f};// ~ -44 dBFS: nothing quieter is ever speech
    float relativeThreshold{0.08f};// a frame counts as speech above 8% of the peak frame
    double minSpeechMs{60.0};      // ignore isolated blips shorter than this
};

// Half-open sample range [startSample, endSample) that should be transcribed,
// plus how much was trimmed. When no speech is found, hasSpeech is false and the
// range covers the whole buffer (the caller's SpeechGate decides what to do).
struct VadResult {
    bool hasSpeech{false};
    std::size_t startSample{0};
    std::size_t endSample{0};
    double trimmedLeadingMs{0.0};
    double trimmedTrailingMs{0.0};
    double keptSeconds{0.0};
    double originalSeconds{0.0};
};

// Finds the speech span in mono PCM (float in [-1, 1]). Pure, no I/O - unit tested.
[[nodiscard]] VadResult detectSpeech(const std::vector<float>& pcmMono, int sampleRate,
                                     const VadSettings& settings = {});

// Convenience: returns a copy trimmed to the detected speech span. If no speech is
// found, returns the input unchanged so the caller's own gate can reject it.
[[nodiscard]] std::vector<float> trimToSpeech(const std::vector<float>& pcmMono, int sampleRate,
                                              const VadSettings& settings = {});

}  // namespace whisperflow
