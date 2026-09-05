#include "AudioCapture.h"

#include <utility>

namespace whisperflow {

AudioCapture::AudioCapture(AudioHandler onAudioData)
    : onAudioData_(std::move(onAudioData)) {}

AudioCapture::~AudioCapture() {
    stopRecording();
}

void AudioCapture::startRecording() {
    if (isRecording_) {
        return;
    }

    isRecording_ = true;

    // Stub: WASAPI/loopback capture will replace this. Emit a short silence buffer
    // so the registered handler can be exercised during bring-up.
    if (onAudioData_) {
        const std::vector<float> silence(160, 0.0f);  // 10 ms at 16 kHz
        onAudioData_(silence);
    }
}

void AudioCapture::stopRecording() {
    isRecording_ = false;
}

bool AudioCapture::isRecording() const noexcept {
    return isRecording_;
}

}  // namespace whisperflow
