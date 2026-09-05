#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace whisperflow {

// Streaming linear resampler. Stateful: inPos_ carries the fractional position
// across calls so that consecutive chunks stay phase-continuous.
//
// NOTE: this is deliberately the same algorithm AudioCapture uses on the capture
// thread. It has no anti-aliasing filter, so for anything other than 1:1 or
// upsampling it is a placeholder - see docs/STATUS.md (accuracy follow-up).
class Resampler {
public:
    Resampler(uint32_t inRate, uint32_t outRate)
        : inRate_(inRate), outRate_(outRate) {}

    std::vector<float> process(const float* input, size_t numFrames) {
        if (inRate_ == outRate_) {
            return std::vector<float>(input, input + numFrames);
        }

        std::vector<float> output;
        if (numFrames == 0) {
            return output;
        }

        const double ratio = static_cast<double>(inRate_) / static_cast<double>(outRate_);
        output.reserve(static_cast<size_t>(static_cast<double>(numFrames) / ratio) + 2);

        while (inPos_ < static_cast<double>(numFrames)) {
            const size_t idx = static_cast<size_t>(inPos_);
            const double frac = inPos_ - static_cast<double>(idx);

            const float s0 = input[idx];
            const float s1 = (idx + 1 < numFrames) ? input[idx + 1] : s0;
            output.push_back(s0 + static_cast<float>(frac) * (s1 - s0));

            inPos_ += ratio;
        }

        inPos_ -= static_cast<double>(numFrames);
        return output;
    }

    void reset() noexcept { inPos_ = 0.0; }

private:
    uint32_t inRate_;
    uint32_t outRate_;
    double inPos_{0.0};
};

}  // namespace whisperflow
