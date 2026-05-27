#include "audio/VoiceActivityDetector.h"

#include <fvad.h>
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc/modules/interface/module_common_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace voice_agent {

namespace {

constexpr int kProcessingFrameMs = 10;
constexpr int kProcessingSampleRate = 16000;
constexpr std::size_t kProcessingFrameSamples = kProcessingSampleRate * kProcessingFrameMs / 1000;
constexpr int kVadMode = 3;
constexpr float kMinRms = 250.0f;
constexpr float kNoiseFloorMultiplier = 3.0f;

std::size_t FramesFromMilliseconds(int sampleRate, int milliseconds) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate) * static_cast<std::size_t>(milliseconds) / 1000);
}

template <std::size_t SampleCount>
int PeakAmplitude(const std::array<std::int16_t, SampleCount>& frame) {
    int maxAmplitude = 0;
    for (std::int16_t sample : frame) {
        const int amplitude = std::abs(static_cast<int>(sample));
        if (amplitude > maxAmplitude) {
            maxAmplitude = amplitude;
        }
    }

    return maxAmplitude;
}

template <std::size_t SampleCount>
float FrameRms(const std::array<std::int16_t, SampleCount>& frame) {
    double sum = 0.0;
    for (std::int16_t sample : frame) {
        const double value = static_cast<double>(sample);
        sum += value * value;
    }

    return static_cast<float>(std::sqrt(sum / static_cast<double>(frame.size())));
}

void InitializeFrame(webrtc::AudioFrame& frame, int sampleRate, std::size_t samplesPerChannel) {
    frame.sample_rate_hz_ = sampleRate;
    frame.num_channels_ = 1;
    frame.samples_per_channel_ = samplesPerChannel;
    std::memset(frame.data_, 0, sizeof(frame.data_));
}

template <std::size_t SampleCount>
void AppendFrame(std::vector<char>& destination, const std::array<std::int16_t, SampleCount>& frame) {
    const auto* byteData = reinterpret_cast<const char*>(frame.data());
    destination.insert(destination.end(), byteData, byteData + sizeof(frame));
}

template <std::size_t SampleCount>
void AppendPreRoll(std::vector<char>& preRollData, const std::array<std::int16_t, SampleCount>& frame, std::size_t maxBytes) {
    AppendFrame(preRollData, frame);
    if (preRollData.size() <= maxBytes) {
        return;
    }

    preRollData.erase(preRollData.begin(), preRollData.begin() + static_cast<std::ptrdiff_t>(preRollData.size() - maxBytes));
}

}  // namespace

class VoiceActivityDetector::Impl final {
public:
    explicit Impl(const VadSettings& settings, std::size_t bytesPerFrame)
        : settings(settings),
          bytesPerFrame(bytesPerFrame),
          processingFrameSamples(FramesFromMilliseconds(settings.sampleRate, kProcessingFrameMs)),
          processingFrameBytes(processingFrameSamples * bytesPerFrame),
          speechStartFrames(FramesFromMilliseconds(settings.sampleRate, settings.startSpeechMs)),
          trailingSilenceFrames(FramesFromMilliseconds(settings.sampleRate, settings.endSilenceMs)),
          preRollBytes(FramesFromMilliseconds(settings.sampleRate, settings.preRollMs) * bytesPerFrame),
          warmupFrames(std::max<int>(1, settings.startSpeechMs / kProcessingFrameMs)),
          noiseFloorRms(kMinRms) {
        if (bytesPerFrame != sizeof(std::int16_t)) {
            throw std::runtime_error("VoiceActivityDetector only supports 16-bit mono PCM input.");
        }
        if (settings.sampleRate != kProcessingSampleRate || processingFrameSamples != kProcessingFrameSamples) {
            throw std::runtime_error("VoiceActivityDetector currently requires 16 kHz mono PCM input.");
        }

        apm = webrtc::AudioProcessing::Create();
        if (apm == nullptr) {
            throw std::runtime_error("Failed to create WebRTC AudioProcessing instance.");
        }

        vad = fvad_new();
        if (vad == nullptr) {
            delete apm;
            apm = nullptr;
            throw std::runtime_error("Failed to create libfvad instance.");
        }

        apm->echo_cancellation()->Enable(true);
        apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kHighSuppression);
        apm->noise_suppression()->Enable(true);
        apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kHigh);
        apm->gain_control()->Enable(false);

        if (fvad_set_mode(vad, kVadMode) < 0) {
            Cleanup();
            throw std::runtime_error("Failed to configure libfvad mode.");
        }
        if (fvad_set_sample_rate(vad, settings.sampleRate) < 0) {
            Cleanup();
            throw std::runtime_error("Failed to configure libfvad sample rate.");
        }

        InitializeFrame(captureFrame_, settings.sampleRate, processingFrameSamples);
        InitializeFrame(reverseFrame_, settings.sampleRate, processingFrameSamples);
    }

    ~Impl() {
        Cleanup();
    }
    void Cleanup() {
        if (vad != nullptr) {
            fvad_free(vad);
            vad = nullptr;
        }
        delete apm;
        apm = nullptr;
    }

    VadSettings settings;
    std::size_t bytesPerFrame;
    std::size_t processingFrameSamples;
    std::size_t processingFrameBytes;
    std::size_t speechStartFrames;
    std::size_t trailingSilenceFrames;
    std::size_t preRollBytes;
    std::vector<char> preRollData;
    std::vector<char> pendingBytes;
    std::size_t consecutiveSpeechFrames = 0;
    std::size_t consecutiveSilenceFrames = 0;
    int sessionPeakAmplitude = 0;
    bool detectedSpeech = false;
    int warmupFrames;
    float noiseFloorRms;
    float rmsAccum = 0.0f;
    int rmsCount = 0;
    webrtc::AudioProcessing* apm = nullptr;
    Fvad* vad = nullptr;
    webrtc::AudioFrame captureFrame_;
    webrtc::AudioFrame reverseFrame_;
};

namespace {

using ProcessingBuffer = std::array<std::int16_t, kProcessingFrameSamples>;

bool DecodeFrame(VoiceActivityDetector::Impl& impl, ProcessingBuffer& frame) {
    if (impl.pendingBytes.size() < sizeof(frame)) {
        return false;
    }

    std::memcpy(frame.data(), impl.pendingBytes.data(), sizeof(frame));
    impl.pendingBytes.erase(impl.pendingBytes.begin(), impl.pendingBytes.begin() + static_cast<std::ptrdiff_t>(sizeof(frame)));
    return true;
}

bool ProcessFrame(VoiceActivityDetector::Impl& impl, const ProcessingBuffer& inputFrame, ProcessingBuffer& cleanedFrame, float& rms) {
    std::memcpy(impl.captureFrame_.data_, inputFrame.data(), sizeof(inputFrame));
    if (impl.apm->ProcessReverseStream(&impl.reverseFrame_) != 0) {
        throw std::runtime_error("WebRTC reverse stream processing failed.");
    }
    if (impl.apm->ProcessStream(&impl.captureFrame_) != 0) {
        throw std::runtime_error("WebRTC capture stream processing failed.");
    }

    std::copy_n(impl.captureFrame_.data_, cleanedFrame.size(), cleanedFrame.begin());
    rms = FrameRms(cleanedFrame);

    const int vadResult = fvad_process(impl.vad, cleanedFrame.data(), static_cast<int>(cleanedFrame.size()));
    if (vadResult < 0) {
        throw std::runtime_error("libfvad failed to process a capture frame.");
    }

    if (impl.warmupFrames > 0) {
        impl.rmsAccum += rms;
        ++impl.rmsCount;
        --impl.warmupFrames;
        if (impl.warmupFrames == 0 && impl.rmsCount > 0) {
            impl.noiseFloorRms = std::max(kMinRms, impl.rmsAccum / static_cast<float>(impl.rmsCount));
        }
        return false;
    }

    if (vadResult != 1) {
        impl.noiseFloorRms = 0.98f * impl.noiseFloorRms + 0.02f * rms;
    }

    const float threshold = std::max(kMinRms, impl.noiseFloorRms * kNoiseFloorMultiplier);
    return vadResult == 1 && rms >= threshold;
}

}  // namespace

VoiceActivityDetector::VoiceActivityDetector(const VadSettings& settings, std::size_t bytesPerFrame)
    : impl_(std::make_unique<Impl>(settings, bytesPerFrame)) {}

VoiceActivityDetector::~VoiceActivityDetector() = default;

VoiceActivityDetector::VoiceActivityDetector(VoiceActivityDetector&&) noexcept = default;

VoiceActivityDetector& VoiceActivityDetector::operator=(VoiceActivityDetector&&) noexcept = default;

VadChunkResult VoiceActivityDetector::ProcessChunk(const char* chunkData, std::size_t capturedFrames, std::vector<char>& pcmData) {
    if (!impl_->settings.enabled) {
        const std::size_t byteCount = capturedFrames * impl_->bytesPerFrame;
        pcmData.insert(pcmData.end(), chunkData, chunkData + byteCount);
        return {};
    }

    const std::size_t byteCount = capturedFrames * impl_->bytesPerFrame;
    impl_->pendingBytes.insert(impl_->pendingBytes.end(), chunkData, chunkData + byteCount);

    VadChunkEvent event = VadChunkEvent::None;
    ProcessingBuffer inputFrame{};
    ProcessingBuffer cleanedFrame{};
    while (DecodeFrame(*impl_, inputFrame)) {
        float rms = 0.0f;
        const bool frameHasSpeech = ProcessFrame(*impl_, inputFrame, cleanedFrame, rms);
        impl_->sessionPeakAmplitude = std::max(impl_->sessionPeakAmplitude, PeakAmplitude(cleanedFrame));

        if (!impl_->detectedSpeech) {
            AppendPreRoll(impl_->preRollData, cleanedFrame, impl_->preRollBytes);
            if (frameHasSpeech) {
                impl_->consecutiveSpeechFrames += impl_->processingFrameSamples;
                if (impl_->consecutiveSpeechFrames >= impl_->speechStartFrames) {
                    impl_->detectedSpeech = true;
                    pcmData.insert(pcmData.end(), impl_->preRollData.begin(), impl_->preRollData.end());
                    impl_->preRollData.clear();
                    event = VadChunkEvent::SpeechStarted;
                }
            } else {
                impl_->consecutiveSpeechFrames = 0;
            }

            continue;
        }

        AppendFrame(pcmData, cleanedFrame);
        if (frameHasSpeech) {
            impl_->consecutiveSilenceFrames = 0;
            continue;
        }

        impl_->consecutiveSilenceFrames += impl_->processingFrameSamples;
        if (impl_->consecutiveSilenceFrames >= impl_->trailingSilenceFrames) {
            return {VadChunkEvent::SpeechEnded, false};
        }
    }

    return {event, true};
}

bool VoiceActivityDetector::DetectedSpeech() const {
    return impl_->detectedSpeech;
}

int VoiceActivityDetector::SessionPeakAmplitude() const {
    return impl_->sessionPeakAmplitude;
}

std::size_t VoiceActivityDetector::ConsecutiveSilenceFrames() const {
    return impl_->consecutiveSilenceFrames;
}

}  // namespace voice_agent