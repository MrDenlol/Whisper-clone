#pragma once

#include <functional>
#include <vector>
#include <memory>

namespace whisperflow {

// Captures PCM float samples from microphone via WASAPI and forwards them to a user-provided handler.
// Composition over inheritance: the data path is a std::function, not a virtual interface.
class AudioCapture {
public:
    using AudioHandler = std::function<void(const std::vector<float>& samples)>;

    explicit AudioCapture(AudioHandler onAudioData);
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;
    AudioCapture(AudioCapture&&) noexcept;
    AudioCapture& operator=(AudioCapture&&) noexcept;

    void startRecording();
    void stopRecording();

    [[nodiscard]] bool isRecording() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace whisperflow
