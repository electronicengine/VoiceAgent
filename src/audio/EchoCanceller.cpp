#include "audio/EchoCanceller.h"

#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace voice_agent {

class EchoCanceller::Impl final {
public:
    explicit Impl(int sampleRate)
        : sampleRate_(sampleRate),
          frameSamples_(static_cast<std::size_t>(sampleRate) * 10 / 1000),
          streamConfig_(sampleRate, 1),
          apm_(webrtc::AudioProcessingBuilder().Create()) {
        if (apm_ == nullptr) {
            throw std::runtime_error("Failed to create WebRTC AudioProcessing instance.");
        }

        webrtc::AudioProcessing::Config config;
        // AEC3 (default echo controller in webrtc-audio-processing v2).
        config.echo_canceller.enabled = true;
        config.echo_canceller.mobile_mode = false;
        config.echo_canceller.enforce_high_pass_filtering = true;
        config.high_pass_filter.enabled = true;

        config.noise_suppression.enabled = true;
        config.noise_suppression.level =
            webrtc::AudioProcessing::Config::NoiseSuppression::kHigh;

        // Keep gain control off to match prior behavior.
        config.gain_controller1.enabled = false;
        config.gain_controller2.enabled = false;

        apm_->ApplyConfig(config);
    }

    int sampleRate_;
    std::size_t frameSamples_;
    webrtc::StreamConfig streamConfig_;
    int streamDelayMs_ = 0;
    rtc::scoped_refptr<webrtc::AudioProcessing> apm_;
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
    // ProcessReverseStream may write back to the buffer; use a scratch output
    // so the caller's buffer stays untouched.
    std::vector<std::int16_t> scratch(sampleCount);
    if (impl_->apm_->ProcessReverseStream(samples, impl_->streamConfig_,
                                          impl_->streamConfig_, scratch.data()) != 0) {
        throw std::runtime_error("WebRTC reverse stream processing failed.");
    }
}

void EchoCanceller::ProcessCaptureFrame(std::int16_t* samples, std::size_t sampleCount) {
    if (sampleCount != frameSamples_) {
        throw std::runtime_error("EchoCanceller::ProcessCaptureFrame requires exactly 10 ms frames.");
    }
    impl_->apm_->set_stream_delay_ms(impl_->streamDelayMs_);
    if (impl_->apm_->ProcessStream(samples, impl_->streamConfig_,
                                   impl_->streamConfig_, samples) != 0) {
        throw std::runtime_error("WebRTC capture stream processing failed.");
    }
}

void EchoCanceller::SetStreamDelayMs(int delayMs) {
    impl_->streamDelayMs_ = std::max(0, delayMs);
}

void EchoCanceller::ClearReverseHistory() {
    // Push several silent reverse frames to flush AEC3 reference history.
    std::vector<std::int16_t> silence(impl_->frameSamples_, 0);
    std::vector<std::int16_t> scratch(impl_->frameSamples_, 0);
    for (int i = 0; i < 5; ++i) {
        impl_->apm_->ProcessReverseStream(silence.data(), impl_->streamConfig_,
                                          impl_->streamConfig_, scratch.data());
    }
}

}  // namespace voice_agent
