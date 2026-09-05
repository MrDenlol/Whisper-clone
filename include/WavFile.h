#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace whisperflow {

// Audio decoded to exactly what the recognizer needs: mono, 16 kHz, float [-1, 1].
struct WavAudio {
    std::vector<float> samples;
    int sourceSampleRate{0};
    int sourceChannels{0};
    double durationSeconds{0.0};
};

// Reads PCM16 / PCM32 / float32 WAV (any channel count, any sample rate),
// downmixes to mono and resamples to 16 kHz.
bool readWavAsMono16k(const std::filesystem::path& path, WavAudio& outAudio, std::string& outError);

// Writes mono 16 kHz audio as a 16-bit PCM WAV file.
bool writeWavMono16k(const std::filesystem::path& path, const std::vector<float>& samplesMono16k,
                     std::string& outError);

}  // namespace whisperflow
