#include "AudioCapture.h"

#include <iostream>

int main() {
    whisperflow::AudioCapture capture([](const std::vector<float>& samples) {
        std::cout << "Received " << samples.size() << " samples\n";
        if (!samples.empty()) {
            std::cout << "  first sample: " << samples.front() << '\n';
        }
    });

    capture.startRecording();
    std::cout << "Recording: " << (capture.isRecording() ? "yes" : "no") << '\n';
    capture.stopRecording();

    return 0;
}
