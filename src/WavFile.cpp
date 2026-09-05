#include "WavFile.h"

#include <cstdint>
#include <cstring>
#include <fstream>

#include "Resampler.h"

namespace whisperflow {
namespace {

constexpr int kTargetSampleRate = 16000;

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void writeU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void writeTag(std::vector<uint8_t>& out, const char (&tag)[5]) {
    out.push_back(static_cast<uint8_t>(tag[0]));
    out.push_back(static_cast<uint8_t>(tag[1]));
    out.push_back(static_cast<uint8_t>(tag[2]));
    out.push_back(static_cast<uint8_t>(tag[3]));
}

bool tagEquals(const uint8_t* p, const char (&tag)[5]) {
    return p[0] == static_cast<uint8_t>(tag[0]) && p[1] == static_cast<uint8_t>(tag[1]) &&
           p[2] == static_cast<uint8_t>(tag[2]) && p[3] == static_cast<uint8_t>(tag[3]);
}

}  // namespace

bool readWavAsMono16k(const std::filesystem::path& path, WavAudio& outAudio, std::string& outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        outError = "Cannot open WAV file: " + path.string();
        return false;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44) {
        outError = "File is too small to be a WAV file: " + path.string();
        return false;
    }

    if (!tagEquals(bytes.data(), "RIFF") || !tagEquals(bytes.data() + 8, "WAVE")) {
        outError = "Not a RIFF/WAVE file: " + path.string();
        return false;
    }

    std::size_t offset = 12;
    bool haveFormat = false;
    uint16_t formatTag = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataPtr = nullptr;
    std::size_t dataSize = 0;

    while (offset + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + offset;
        const uint32_t chunkSize = readU32(chunk + 4);
        const uint8_t* payload = chunk + 8;

        if (tagEquals(chunk, "fmt ") && chunkSize >= 16 && offset + 8 + 16 <= bytes.size()) {
            formatTag = readU16(payload);
            channels = readU16(payload + 2);
            sampleRate = readU32(payload + 4);
            bitsPerSample = readU16(payload + 14);

            // WAVE_FORMAT_EXTENSIBLE carries the real format in the sub-format GUID.
            if (formatTag == 0xFFFE && chunkSize >= 26 && offset + 8 + 26 <= bytes.size()) {
                const uint16_t subFormat = readU16(payload + 24);
                if (subFormat == 0x0001 || subFormat == 0x0003) {
                    formatTag = subFormat;
                }
                const uint16_t validBits = readU16(payload + 18);
                if (validBits != 0) {
                    bitsPerSample = validBits;
                }
            }
            haveFormat = true;
        } else if (tagEquals(chunk, "data")) {
            const std::size_t available = bytes.size() - (offset + 8);
            dataSize = (chunkSize <= available) ? chunkSize : available;
            dataPtr = payload;
        }

        offset += 8 + chunkSize + (chunkSize % 2);  // chunks are word aligned
    }

    if (!haveFormat) {
        outError = "WAV file has no 'fmt ' chunk: " + path.string();
        return false;
    }
    if (dataPtr == nullptr || dataSize == 0) {
        outError = "WAV file has no audio data: " + path.string();
        return false;
    }
    if (channels == 0 || sampleRate == 0) {
        outError = "WAV file has an invalid format chunk: " + path.string();
        return false;
    }

    const bool isFloat = (formatTag == 0x0003);
    const bool isPcm = (formatTag == 0x0001);
    if (!isFloat && !isPcm) {
        outError = "Unsupported WAV format tag " + std::to_string(formatTag) +
                   " (only PCM and IEEE float are supported).";
        return false;
    }

    const std::size_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) {
        outError = "Unsupported bits per sample: " + std::to_string(bitsPerSample);
        return false;
    }
    const std::size_t frameSize = bytesPerSample * channels;
    if (frameSize == 0) {
        outError = "Unsupported channel count: " + std::to_string(channels);
        return false;
    }

    const std::size_t frameCount = dataSize / frameSize;
    std::vector<float> mono;
    mono.reserve(frameCount);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const uint8_t* p = dataPtr + frame * frameSize;
        float sum = 0.0f;

        for (uint16_t channel = 0; channel < channels; ++channel) {
            const uint8_t* sample = p + static_cast<std::size_t>(channel) * bytesPerSample;
            if (isFloat && bitsPerSample == 32) {
                float value = 0.0f;
                std::memcpy(&value, sample, sizeof(value));
                sum += value;
            } else if (isPcm && bitsPerSample == 16) {
                const int16_t raw = static_cast<int16_t>(static_cast<uint16_t>(sample[0]) |
                                                         (static_cast<uint16_t>(sample[1]) << 8));
                sum += static_cast<float>(raw) / 32768.0f;
            } else if (isPcm && bitsPerSample == 32) {
                const int32_t raw = static_cast<int32_t>(readU32(sample));
                sum += static_cast<float>(static_cast<double>(raw) / 2147483648.0);
            } else if (isPcm && bitsPerSample == 8) {
                sum += (static_cast<float>(sample[0]) - 128.0f) / 128.0f;
            } else {
                outError = "Unsupported sample width: " + std::to_string(bitsPerSample) + " bits.";
                return false;
            }
        }

        mono.push_back(sum / static_cast<float>(channels));
    }

    if (sampleRate == static_cast<uint32_t>(kTargetSampleRate)) {
        outAudio.samples = std::move(mono);
    } else {
        Resampler resampler(sampleRate, static_cast<uint32_t>(kTargetSampleRate));
        outAudio.samples = resampler.process(mono.data(), mono.size());
    }

    outAudio.sourceSampleRate = static_cast<int>(sampleRate);
    outAudio.sourceChannels = static_cast<int>(channels);
    outAudio.durationSeconds = outAudio.samples.empty()
                                   ? 0.0
                                   : static_cast<double>(outAudio.samples.size()) / kTargetSampleRate;
    return true;
}

bool writeWavMono16k(const std::filesystem::path& path, const std::vector<float>& samplesMono16k,
                     std::string& outError) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    constexpr uint32_t kSampleRate = static_cast<uint32_t>(kTargetSampleRate);
    constexpr uint16_t kChannels = 1;
    constexpr uint16_t kBits = 16;
    const uint32_t dataSize = static_cast<uint32_t>(samplesMono16k.size() * 2);

    std::vector<uint8_t> out;
    out.reserve(44 + dataSize);
    writeTag(out, "RIFF");
    writeU32(out, 36 + dataSize);
    writeTag(out, "WAVE");
    writeTag(out, "fmt ");
    writeU32(out, 16);
    writeU16(out, 0x0001);  // PCM
    writeU16(out, kChannels);
    writeU32(out, kSampleRate);
    writeU32(out, kSampleRate * kChannels * (kBits / 8));
    writeU16(out, static_cast<uint16_t>(kChannels * (kBits / 8)));
    writeU16(out, kBits);
    writeTag(out, "data");
    writeU32(out, dataSize);

    for (float value : samplesMono16k) {
        float clamped = value;
        if (clamped > 1.0f) {
            clamped = 1.0f;
        } else if (clamped < -1.0f) {
            clamped = -1.0f;
        }
        const auto raw = static_cast<int16_t>(clamped * 32767.0f);
        writeU16(out, static_cast<uint16_t>(raw));
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        outError = "Cannot write WAV file: " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file.good()) {
        outError = "Failed while writing WAV file: " + path.string();
        return false;
    }
    return true;
}

}  // namespace whisperflow
