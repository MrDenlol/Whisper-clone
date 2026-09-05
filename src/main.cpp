#include "AudioCapture.h"

#include <iostream>
#include <atomic>
#include <chrono>

int main() {
    std::cout << "=== WhisperFlowClone (WASAPI Audio Capture Demo) ===\n";
    std::cout << "Target format: 16 kHz, Mono, Float32 (Whisper-ready)\n\n";

    std::atomic<size_t> totalSamples{0};
    auto startTime = std::chrono::steady_clock::now();

    whisperflow::AudioCapture capture([&totalSamples](const std::vector<float>& samples) {
        totalSamples += samples.size();
        std::cout << "[Audio] Received chunk of " << samples.size() 
                  << " samples (Total: " << totalSamples.load() << ")\r" << std::flush;
    });

    std::cout << "Press [Enter] to START recording...";
    std::cin.get();

    startTime = std::chrono::steady_clock::now();
    totalSamples = 0;
    capture.startRecording();

    std::cout << "Recording active! Speak into your microphone.\n";
    std::cout << "Press [Enter] to STOP recording...\n";
    std::cin.get();

    capture.stopRecording();
    auto endTime = std::chrono::steady_clock::now();

    auto durationSec = std::chrono::duration<double>(endTime - startTime).count();
    size_t samplesCount = totalSamples.load();
    double samplesPerSec = (durationSec > 0.0) ? static_cast<double>(samplesCount) / durationSec : 0.0;

    std::cout << "\n--- Recording Summary ---\n";
    std::cout << "Duration:       " << durationSec << " seconds\n";
    std::cout << "Total samples:  " << samplesCount << "\n";
    std::cout << "Sample rate:    " << samplesPerSec << " samples/sec (Target: 16000)\n";
    std::cout << "Demo finished successfully.\n";

    return 0;
}
