#include "audio/VadDetector.h"

#include <fvad.h>

#include <cmath>
#include <stdexcept>

namespace voice_agent {

VadDetector::VadDetector(int sampleRate, int aggressiveness)
    : vad_(fvad_new()),
      frameSamples_(static_cast<std::size_t>(sampleRate) * 10 / 1000) {
    if (vad_ == nullptr) {
        throw std::runtime_error("Failed to create libfvad instance.");
    }
    if (fvad_set_mode(vad_, aggressiveness) < 0) {
        fvad_free(vad_);
        vad_ = nullptr;
        throw std::runtime_error("Failed to configure libfvad mode.");
    }
    if (fvad_set_sample_rate(vad_, sampleRate) < 0) {
        fvad_free(vad_);
        vad_ = nullptr;
        throw std::runtime_error("Failed to configure libfvad sample rate.");
    }
}

VadDetector::~VadDetector() {
    if (vad_ != nullptr) {
        fvad_free(vad_);
    }
}

bool VadDetector::IsSpeech(const std::int16_t* samples, std::size_t sampleCount) const {
    if (sampleCount != frameSamples_) {
        throw std::runtime_error("VadDetector::IsSpeech requires exactly 10 ms frames.");
    }
    const int result = fvad_process(vad_, samples, static_cast<int>(sampleCount));
    if (result < 0) {
        throw std::runtime_error("libfvad failed to process a capture frame.");
    }
    return result == 1;
}

float VadDetector::Rms(const std::int16_t* samples, std::size_t sampleCount) {
    double sum = 0.0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const double value = static_cast<double>(samples[i]);
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(sampleCount)));
}

}  // namespace voice_agent
