#include "audio/EchoCanceller.h"

#include <webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc/modules/interface/module_common_types.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace voice_agent {

class EchoCanceller::Impl final {
public:
    explicit Impl(int sampleRate)
        : sampleRate_(sampleRate),
          frameSamples_(static_cast<std::size_t>(sampleRate) * 10 / 1000),
          apm_(webrtc::AudioProcessing::Create()) {
        if (apm_ == nullptr) {
            throw std::runtime_error("Failed to create WebRTC AudioProcessing instance.");
        }

        apm_->echo_cancellation()->Enable(true);
        apm_->echo_cancellation()->set_suppression_level(
            webrtc::EchoCancellation::kHighSuppression);
        apm_->noise_suppression()->Enable(true);
        apm_->noise_suppression()->set_level(webrtc::NoiseSuppression::kHigh);
        apm_->gain_control()->Enable(false);

        InitFrame(captureFrame_);
        InitFrame(reverseFrame_);
    }

    ~Impl() {
        delete apm_;
    }

    void InitFrame(webrtc::AudioFrame& frame) {
        frame.sample_rate_hz_ = sampleRate_;
        frame.num_channels_ = 1;
        frame.samples_per_channel_ = frameSamples_;
        std::memset(frame.data_, 0, sizeof(frame.data_));
    }

    int sampleRate_;
    std::size_t frameSamples_;
    int streamDelayMs_ = 0;
    webrtc::AudioProcessing* apm_ = nullptr;
    webrtc::AudioFrame captureFrame_{};
    webrtc::AudioFrame reverseFrame_{};
};

EchoCanceller::EchoCanceller(int sampleRate)
    : impl_(std::make_unique<Impl>(sampleRate)),
      sampleRate_(sampleRate),
      frameSamples_(impl_->frameSamples_) {}

EchoCanceller::~EchoCanceller() = default;

void EchoCanceller::PushReverseFrame(const std::int16_t* samples, std::size_t sampleCount) {
    if (sampleCount != frameSamples_) {
        throw std::runtime_error("EchoCanceller::PushReverseFrame requires exactly 10 ms frames.");
    }
    std::memcpy(impl_->reverseFrame_.data_, samples, sizeof(std::int16_t) * sampleCount);
    if (impl_->apm_->ProcessReverseStream(&impl_->reverseFrame_) != 0) {
        throw std::runtime_error("WebRTC reverse stream processing failed.");
    }
}

void EchoCanceller::ProcessCaptureFrame(std::int16_t* samples, std::size_t sampleCount) {
    if (sampleCount != frameSamples_) {
        throw std::runtime_error("EchoCanceller::ProcessCaptureFrame requires exactly 10 ms frames.");
    }
    std::memcpy(impl_->captureFrame_.data_, samples, sizeof(std::int16_t) * sampleCount);
    // WebRTC APM requires set_stream_delay_ms before every ProcessStream call.
    impl_->apm_->set_stream_delay_ms(impl_->streamDelayMs_);
    if (impl_->apm_->ProcessStream(&impl_->captureFrame_) != 0) {
        throw std::runtime_error("WebRTC capture stream processing failed.");
    }
    std::memcpy(samples, impl_->captureFrame_.data_, sizeof(std::int16_t) * sampleCount);
}

void EchoCanceller::SetStreamDelayMs(int delayMs) {
    impl_->streamDelayMs_ = std::max(0, delayMs);
}

void EchoCanceller::ClearReverseHistory() {
    std::memset(impl_->reverseFrame_.data_, 0, sizeof(impl_->reverseFrame_.data_));
    // Push several silent reverse frames to flush the AEC reference history.
    for (int i = 0; i < 5; ++i) {
        impl_->apm_->ProcessReverseStream(&impl_->reverseFrame_);
    }
}

}  // namespace voice_agent
