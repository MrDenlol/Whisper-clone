#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "WavFile.h"
#include "test_framework.h"
#include "test_paths.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void appendTag(std::vector<uint8_t>& out, const char* tag) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(tag[i]));
    }
}

// Builds a PCM16 WAV file with a sine wave in every channel.
std::vector<uint8_t> makePcm16Wav(int sampleRate, int channels, std::size_t framesPerChannel) {
    std::vector<uint8_t> data;
    data.reserve(framesPerChannel * static_cast<std::size_t>(channels) * 2);
    for (std::size_t frame = 0; frame < framesPerChannel; ++frame) {
        const double t = static_cast<double>(frame) / static_cast<double>(sampleRate);
        const auto sample = static_cast<int16_t>(12000.0 * std::sin(2.0 * kPi * 440.0 * t));
        for (int channel = 0; channel < channels; ++channel) {
            const int16_t value = (channel == 0) ? sample : static_cast<int16_t>(-sample);
            appendU16(data, static_cast<uint16_t>(value));
        }
    }

    std::vector<uint8_t> out;
    appendTag(out, "RIFF");
    appendU32(out, static_cast<uint32_t>(36 + data.size()));
    appendTag(out, "WAVE");
    appendTag(out, "fmt ");
    appendU32(out, 16);
    appendU16(out, 1);  // PCM
    appendU16(out, static_cast<uint16_t>(channels));
    appendU32(out, static_cast<uint32_t>(sampleRate));
    appendU32(out, static_cast<uint32_t>(sampleRate * channels * 2));
    appendU16(out, static_cast<uint16_t>(channels * 2));
    appendU16(out, 16);
    appendTag(out, "data");
    appendU32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::string asString(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

}  // namespace

WF_TEST(Wav_roundTripsMono16k) {
    const std::filesystem::path dir = wftest::scratchDirectory("wav");
    const std::filesystem::path path = dir / "roundtrip.wav";

    std::vector<float> samples(16000);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        samples[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * t));
    }

    std::string error;
    WF_CHECK(whisperflow::writeWavMono16k(path, samples, error));

    whisperflow::WavAudio audio;
    WF_CHECK(whisperflow::readWavAsMono16k(path, audio, error));
    WF_CHECK_EQ(audio.sourceSampleRate, 16000);
    WF_CHECK_EQ(audio.sourceChannels, 1);
    WF_CHECK_EQ(audio.samples.size(), samples.size());
    WF_CHECK(std::fabs(audio.samples[100] - samples[100]) < 0.001);
    WF_CHECK(std::fabs(audio.durationSeconds - 1.0) < 0.001);
}

WF_TEST(Wav_downmixesAndResamplesTo16k) {
    const std::filesystem::path dir = wftest::scratchDirectory("wav48k");
    const std::filesystem::path path = dir / "stereo48k.wav";

    // 1 second of 48 kHz stereo, anti-phase: the mono downmix must be silence.
    const std::vector<uint8_t> bytes = makePcm16Wav(48000, 2, 48000);
    WF_CHECK(wftest::writeFile(path, asString(bytes)));

    whisperflow::WavAudio audio;
    std::string error;
    WF_CHECK(whisperflow::readWavAsMono16k(path, audio, error));
    WF_CHECK_EQ(audio.sourceSampleRate, 48000);
    WF_CHECK_EQ(audio.sourceChannels, 2);
    WF_CHECK(audio.samples.size() >= 15900 && audio.samples.size() <= 16100);

    float peak = 0.0f;
    for (float value : audio.samples) {
        peak = std::max(peak, std::fabs(value));
    }
    WF_CHECK(peak < 0.001f);
}

WF_TEST(Wav_rejectsInvalidInput) {
    const std::filesystem::path dir = wftest::scratchDirectory("wavbad");

    whisperflow::WavAudio audio;
    std::string error;
    WF_CHECK(!whisperflow::readWavAsMono16k(dir / "absent.wav", audio, error));
    WF_CHECK(!error.empty());

    const std::filesystem::path garbage = dir / "garbage.wav";
    WF_CHECK(wftest::writeFile(garbage, "this is definitely not a RIFF file at all, nope"));
    error.clear();
    WF_CHECK(!whisperflow::readWavAsMono16k(garbage, audio, error));
    WF_CHECK(error.find("RIFF") != std::string::npos);
}
