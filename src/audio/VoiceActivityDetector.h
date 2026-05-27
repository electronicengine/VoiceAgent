#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace voice_agent {

enum class VadChunkEvent {
    None,
    SpeechStarted,
    SpeechEnded,
};

struct VadChunkResult {
    VadChunkEvent event = VadChunkEvent::None;
    bool shouldContinue = true;
};

struct VadSettings {
    bool enabled = false;
    int sampleRate = 16000;
    int startSpeechMs = 200;
    int endSilenceMs = 800;
    int preRollMs = 200;
    int amplitudeThreshold = 900;
};

class VoiceActivityDetector final {
public:
    VoiceActivityDetector(const VadSettings& settings, std::size_t bytesPerFrame);
    ~VoiceActivityDetector();

    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector(VoiceActivityDetector&&) noexcept;
    VoiceActivityDetector& operator=(VoiceActivityDetector&&) noexcept;

    VadChunkResult ProcessChunk(const char* chunkData, std::size_t capturedFrames, std::vector<char>& pcmData);

    bool DetectedSpeech() const;
    int SessionPeakAmplitude() const;
    std::size_t ConsecutiveSilenceFrames() const;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

}  // namespace voice_agent