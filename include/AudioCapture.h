#pragma once

#include <functional>
#include <vector>

namespace whisperflow {

// Captures PCM float samples and forwards them to a user-provided handler.
// Composition over inheritance: the data path is a std::function, not a virtual interface.
class AudioCapture {
public:
    using AudioHandler = std::function<void(const std::vector<float>& samples)>;

    explicit AudioCapture(AudioHandler onAudioData);
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;
    AudioCapture(AudioCapture&&) noexcept = default;
    AudioCapture& operator=(AudioCapture&&) noexcept = default;

    void startRecording();
    void stopRecording();

    [[nodiscard]] bool isRecording() const noexcept;

private:
    AudioHandler onAudioData_;
    bool isRecording_{false};
};

}  // namespace whisperflow
