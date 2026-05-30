#pragma once

#include <cstddef>
#include <cstdint>

struct Fvad;

namespace voice_agent {

// Wraps libfvad. Stateless w.r.t. RMS gating — caller decides thresholding.
class VadDetector final {
public:
    explicit VadDetector(int sampleRate = 16000, int aggressiveness = 3);
    ~VadDetector();

    VadDetector(const VadDetector&) = delete;
    VadDetector& operator=(const VadDetector&) = delete;

    // Returns true if libfvad classifies the 10 ms frame as speech.
    bool IsSpeech(const std::int16_t* samples, std::size_t sampleCount) const;

    // Compute RMS of a frame (helper for the noise-floor gate).
    static float Rms(const std::int16_t* samples, std::size_t sampleCount);

private:
    Fvad* vad_ = nullptr;
    std::size_t frameSamples_ = 0;
};

}  // namespace voice_agent
