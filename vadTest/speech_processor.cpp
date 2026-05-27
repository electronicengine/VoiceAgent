#include "speech_processor.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace robot_audio {

namespace {

void initializeFrame(webrtc::AudioFrame& frame) {
    frame.sample_rate_hz_ = kSampleRate;
    frame.num_channels_ = 1;
    frame.samples_per_channel_ = kFrameSamples;
    std::memset(frame.data_, 0, sizeof(frame.data_));
}

}  // namespace

SpeechProcessor::SpeechProcessor()
    : apm_(webrtc::AudioProcessing::Create()),
      vad_(fvad_new()),
      warmupFrames_(kStartupWarmupFrames),
      playbackCooldownFrames_(0),
      noiseFloorRms_(kMinRms),
      rmsAccum_(0.0f),
      rmsCount_(0) {
    if (apm_ == nullptr) {
        throw std::runtime_error("WebRTC AudioProcessing olusturulamadi");
    }
    if (vad_ == nullptr) {
        delete apm_;
        throw std::runtime_error("libfvad olusturulamadi");
    }

    apm_->echo_cancellation()->Enable(true);
    apm_->echo_cancellation()->set_suppression_level(
        webrtc::EchoCancellation::kHighSuppression);
    apm_->noise_suppression()->Enable(true);
    apm_->noise_suppression()->set_level(webrtc::NoiseSuppression::kHigh);
    apm_->gain_control()->Enable(false);

    fvad_set_mode(vad_, 3);
    fvad_set_sample_rate(vad_, kSampleRate);

    initializeFrame(micFrame_);
    initializeFrame(reverseFrame_);
}

SpeechProcessor::~SpeechProcessor() {
    if (vad_ != nullptr) {
        fvad_free(vad_);
    }
    delete apm_;
}

void SpeechProcessor::processReverseFrame(const AudioBuffer& frame) {
    std::memcpy(reverseFrame_.data_, frame.data(), sizeof(int16_t) * frame.size());
    apm_->ProcessReverseStream(&reverseFrame_);
}

void SpeechProcessor::beginPlaybackCooldown() {
    playbackCooldownFrames_ = kPlaybackCooldownFrames;
    std::memset(reverseFrame_.data_, 0, sizeof(int16_t) * kFrameSamples);
}

SpeechProcessor::ProcessResult SpeechProcessor::processCaptureFrame(
    const AudioBuffer& microphoneFrame,
    bool detectionSuppressed) {
    ProcessResult result;

    std::memcpy(micFrame_.data_, microphoneFrame.data(), sizeof(int16_t) * microphoneFrame.size());
    apm_->ProcessStream(&micFrame_);

    std::copy_n(micFrame_.data_, kFrameSamples, result.cleanedFrame.begin());
    result.rms = frameRms(result.cleanedFrame);

    const int isSpeech = fvad_process(vad_, result.cleanedFrame.data(), kFrameSamples);
    if (isSpeech < 0) {
        std::cerr << "VAD isleme hatasi\n";
        return result;
    }

    if (warmupFrames_ > 0) {
        rmsAccum_ += result.rms;
        ++rmsCount_;
        --warmupFrames_;
        if (warmupFrames_ == 0 && rmsCount_ > 0) {
            noiseFloorRms_ = std::max(kMinRms, rmsAccum_ / rmsCount_);
            result.becameReady = true;
        }
        result.noiseFloorRms = noiseFloorRms_;
        return result;
    }

    result.ready = true;

    if (playbackCooldownFrames_ > 0) {
        --playbackCooldownFrames_;
    }

    const bool inCooldown = playbackCooldownFrames_ > 0;
    const bool rawVadPositive = (isSpeech == 1) && !detectionSuppressed && !inCooldown;

    if (!rawVadPositive) {
        noiseFloorRms_ = 0.98f * noiseFloorRms_ + 0.02f * result.rms;
    }

    result.noiseFloorRms = noiseFloorRms_;
    result.threshold = std::max(kMinRms, noiseFloorRms_ * kNoiseFloorMultiplier);
    result.speechDetected = rawVadPositive && (result.rms >= result.threshold);
    return result;
}

}  // namespace robot_audio