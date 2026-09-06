#include "Vad.h"

#include <algorithm>
#include <cmath>

namespace whisperflow {
namespace {

double clampRate(int sampleRate) {
    return (sampleRate > 0) ? static_cast<double>(sampleRate) : 16000.0;
}

std::size_t msToSamples(double ms, double rate) {
    if (ms <= 0.0) {
        return 0;
    }
    return static_cast<std::size_t>(ms * rate / 1000.0);
}

}  // namespace

VadResult detectSpeech(const std::vector<float>& pcmMono, int sampleRate,
                       const VadSettings& settings) {
    const double rate = clampRate(sampleRate);

    VadResult result;
    result.startSample = 0;
    result.endSample = pcmMono.size();
    result.originalSeconds = static_cast<double>(pcmMono.size()) / rate;
    result.keptSeconds = result.originalSeconds;

    std::size_t frameLen = msToSamples(settings.frameMs, rate);
    if (frameLen == 0) {
        frameLen = 1;
    }
    if (pcmMono.empty()) {
        return result;
    }

    const std::size_t frameCount = (pcmMono.size() + frameLen - 1) / frameLen;

    // RMS per frame, and the loudest frame (for the relative threshold).
    std::vector<float> frameRms(frameCount, 0.0f);
    float peakRms = 0.0f;
    for (std::size_t f = 0; f < frameCount; ++f) {
        const std::size_t begin = f * frameLen;
        const std::size_t end = std::min(begin + frameLen, pcmMono.size());
        double sumSquares = 0.0;
        for (std::size_t i = begin; i < end; ++i) {
            const double s = static_cast<double>(pcmMono[i]);
            sumSquares += s * s;
        }
        const std::size_t n = end - begin;
        const float rms = (n > 0) ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(n)))
                                  : 0.0f;
        frameRms[f] = rms;
        peakRms = std::max(peakRms, rms);
    }

    const float threshold = std::max(settings.absoluteRmsFloor,
                                     settings.relativeThreshold * peakRms);

    // First and last frame above the threshold.
    std::size_t firstSpeech = frameCount;
    std::size_t lastSpeech = 0;
    for (std::size_t f = 0; f < frameCount; ++f) {
        if (frameRms[f] >= threshold) {
            firstSpeech = std::min(firstSpeech, f);
            lastSpeech = std::max(lastSpeech, f);
        }
    }

    if (firstSpeech > lastSpeech) {
        // Nothing crossed the threshold: no speech detected.
        result.hasSpeech = false;
        return result;
    }

    const double speechMs =
        static_cast<double>(lastSpeech - firstSpeech + 1) * settings.frameMs;
    if (speechMs < settings.minSpeechMs) {
        result.hasSpeech = false;
        return result;
    }

    const std::size_t padFrames =
        (frameLen > 0) ? (msToSamples(settings.paddingMs, rate) + frameLen - 1) / frameLen : 0;

    const std::size_t startFrame =
        (firstSpeech > padFrames) ? (firstSpeech - padFrames) : 0;
    const std::size_t endFrame = std::min(lastSpeech + padFrames, frameCount - 1);

    result.hasSpeech = true;
    result.startSample = startFrame * frameLen;
    result.endSample = std::min((endFrame + 1) * frameLen, pcmMono.size());

    result.trimmedLeadingMs = static_cast<double>(result.startSample) / rate * 1000.0;
    result.trimmedTrailingMs =
        static_cast<double>(pcmMono.size() - result.endSample) / rate * 1000.0;
    result.keptSeconds = static_cast<double>(result.endSample - result.startSample) / rate;
    return result;
}

std::vector<float> trimToSpeech(const std::vector<float>& pcmMono, int sampleRate,
                                const VadSettings& settings) {
    const VadResult vad = detectSpeech(pcmMono, sampleRate, settings);
    if (!vad.hasSpeech) {
        return pcmMono;
    }
    return std::vector<float>(pcmMono.begin() + static_cast<std::ptrdiff_t>(vad.startSample),
                              pcmMono.begin() + static_cast<std::ptrdiff_t>(vad.endSample));
}

}  // namespace whisperflow
