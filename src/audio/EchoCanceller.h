#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace voice_agent {

// Wraps webrtc::AudioProcessing for AEC (echo cancellation) + noise suppression.
// Operates on fixed 10 ms mono S16LE frames at 16 kHz, matching APM's requirements.
class EchoCanceller final {
public:
    explicit EchoCanceller(int sampleRate = 16000);
    ~EchoCanceller();

    EchoCanceller(const EchoCanceller&) = delete;
    EchoCanceller& operator=(const EchoCanceller&) = delete;

    // Feed a frame of speaker output (the "reference" / far-end signal).
    void PushReverseFrame(const std::int16_t* samples, std::size_t sampleCount);

    // Process a captured mic frame in-place. Applies AEC + NS.
    void ProcessCaptureFrame(std::int16_t* samples, std::size_t sampleCount);

    // Inform APM about playback->capture pipeline delay (ms).
    void SetStreamDelayMs(int delayMs);

    // Reset reverse stream history (call on playback stop / cooldown start).
    void ClearReverseHistory();

    int SampleRate() const noexcept { return sampleRate_; }
    std::size_t FrameSamples() const noexcept { return frameSamples_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    int sampleRate_;
    std::size_t frameSamples_;
};

}  // namespace voice_agent
