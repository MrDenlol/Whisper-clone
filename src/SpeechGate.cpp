#include "SpeechGate.h"

#include <cmath>

namespace whisperflow {
namespace {

bool isWordCharacter(unsigned char byte) {
    // ASCII letters and digits, plus every byte of a UTF-8 multibyte sequence
    // (Cyrillic, umlauts, ...). Punctuation and symbols are not word characters.
    if (byte >= 0x80) {
        return true;
    }
    if (byte >= 'a' && byte <= 'z') {
        return true;
    }
    if (byte >= 'A' && byte <= 'Z') {
        return true;
    }
    return byte >= '0' && byte <= '9';
}

}  // namespace

SpeechVerdict evaluateSpeech(const std::vector<float>& pcmMono16k, int sampleRate,
                             const SpeechGateSettings& settings) {
    SpeechVerdict verdict;

    const double rate = (sampleRate > 0) ? static_cast<double>(sampleRate) : 16000.0;
    verdict.seconds = static_cast<double>(pcmMono16k.size()) / rate;

    if (pcmMono16k.empty()) {
        verdict.reason = "buffer is empty";
        return verdict;
    }

    double sumSquares = 0.0;
    float peak = 0.0f;
    for (float sample : pcmMono16k) {
        const float magnitude = std::fabs(sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    verdict.peak = peak;
    verdict.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(pcmMono16k.size())));

    if (verdict.seconds < settings.minSeconds) {
        verdict.reason = "too short (" + std::to_string(static_cast<int>(verdict.seconds * 1000.0)) +
                         " ms < " + std::to_string(static_cast<int>(settings.minSeconds * 1000.0)) + " ms)";
        return verdict;
    }
    if (verdict.peak < settings.minPeak) {
        verdict.reason = "silence (peak " + std::to_string(verdict.peak) + " < " +
                         std::to_string(settings.minPeak) + ")";
        return verdict;
    }
    if (verdict.rms < settings.minRms) {
        verdict.reason = "silence (rms " + std::to_string(verdict.rms) + " < " +
                         std::to_string(settings.minRms) + ")";
        return verdict;
    }

    verdict.acceptable = true;
    return verdict;
}

bool isMeaningfulText(const std::string& text) {
    int squareDepth = 0;
    int roundDepth = 0;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);

        // Skip the UTF-8 music notes Whisper emits for non-speech audio:
        // U+266A (E2 99 AA) and U+266B (E2 99 AB).
        if (byte == 0xE2 && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0x99 &&
            (static_cast<unsigned char>(text[i + 2]) == 0xAA ||
             static_cast<unsigned char>(text[i + 2]) == 0xAB)) {
            i += 2;
            continue;
        }

        // Skip bracketed annotations such as "[Blues]", "[Music]", "(applause)".
        if (byte == '[') {
            ++squareDepth;
            continue;
        }
        if (byte == ']') {
            if (squareDepth > 0) {
                --squareDepth;
            }
            continue;
        }
        if (byte == '(') {
            ++roundDepth;
            continue;
        }
        if (byte == ')') {
            if (roundDepth > 0) {
                --roundDepth;
            }
            continue;
        }
        if (squareDepth > 0 || roundDepth > 0) {
            continue;
        }

        if (isWordCharacter(byte)) {
            return true;
        }
    }
    return false;
}

}  // namespace whisperflow
