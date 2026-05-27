#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace robot_audio {

constexpr int kSampleRate = 16000;
constexpr int kFrameMs = 10;
constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;
constexpr int kStartSpeechFrames = 3;
constexpr int kStartupWarmupFrames = 30;
constexpr int kPlaybackCooldownFrames = 20;
constexpr float kMinRms = 250.0f;
constexpr float kNoiseFloorMultiplier = 3.0f;
constexpr int kEndSilenceMs = 2000;
constexpr int kEndSilenceFrames = kEndSilenceMs / kFrameMs;
constexpr int kMinSpeechFrames = 10;

using AudioBuffer = std::array<int16_t, kFrameSamples>;

inline float frameRms(const AudioBuffer& data) {
    double sum = 0.0;
    for (int16_t sample : data) {
        const double value = static_cast<double>(sample);
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / data.size()));
}

}  // namespace robot_audio