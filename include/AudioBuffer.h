#pragma once

#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace whisperflow {

// Thread-safe accumulator for the capture -> transcription hand-off.
//
// The WASAPI capture thread calls append() many times per second with small
// chunks; the message-loop thread calls take() once when the hotkey is released.
// A single growing vector with reserved capacity means the tiny callbacks append
// into place instead of allocating a fresh vector each time.
class AudioBuffer {
public:
    // Reserve capacity up front (default ~30 s at 16 kHz) so typical dictation
    // never reallocates on the capture thread. Header-only, no ownership tricks.
    explicit AudioBuffer(std::size_t reserveSamples = 16000 * 30) {
        samples_.reserve(reserveSamples);
    }

    // Append a captured chunk. Cheap: amortised O(n) with no per-call allocation
    // once capacity is reached.
    void append(const std::vector<float>& chunk) {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.insert(samples_.end(), chunk.begin(), chunk.end());
    }

    void append(const float* data, std::size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.insert(samples_.end(), data, data + count);
    }

    // Drop everything but keep the reserved capacity (used at record start).
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
    }

    // Move the accumulated audio out and reset for the next utterance. The
    // returned vector owns the samples; the internal one keeps its capacity.
    [[nodiscard]] std::vector<float> take() {
        std::vector<float> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            out.swap(samples_);
        }
        samples_.reserve(out.capacity());
        return out;
    }

    // Snapshot the current samples without clearing (used by early streaming).
    [[nodiscard]] std::vector<float> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::vector<float> samples_;
};

}  // namespace whisperflow
