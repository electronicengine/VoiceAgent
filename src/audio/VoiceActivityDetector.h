#pragma once

#include "audio/EchoCanceller.h"
#include "audio/VadDetector.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace voice_agent {

enum class VadEvent {
    None,
    SpeechStarted,
    SpeechEnded,
};

struct VadFrameResult {
    VadEvent event = VadEvent::None;
    bool speechActive = false;       // true while we're inside a detected utterance
    bool speechDetectedRaw = false;  // post-AEC + RMS gate per-frame
    float rms = 0.0f;
    float noiseFloorRms = 0.0f;
    // When event == SpeechStarted, carries the pre-roll samples flushed at start.
    std::vector<std::int16_t> preRollPcm;
};

struct VadConfig {
    int sampleRate = 16000;
    int startSpeechMs = 200;
    int endSilenceMs = 800;
    int preRollMs = 200;
    int playbackCooldownMs = 200;
    int aecStreamDelayMs = 40;
    int vadAggressiveness = 3;
    int startupWarmupMs = 300;
};

// Frame-based detector composing EchoCanceller (AEC + NS) and VadDetector (libfvad)
// with adaptive noise-floor RMS gate, pre-roll buffer, and playback cooldown.
//
// Threading: NOT thread-safe; the owner must serialize PushReverseFrame and
// ProcessCaptureFrame (typically by holding a single mutex inside VoiceController).
class VoiceActivityDetector final {
public:
    explicit VoiceActivityDetector(const VadConfig& config);

    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;

    // Feed a 10 ms reverse (speaker) frame for AEC reference.
    void PushReverseFrame(const std::int16_t* samples, std::size_t sampleCount);

    // Process a 10 ms captured mic frame. Writes cleaned PCM into `cleanedOut`
    // (resized to sampleCount) and returns the per-frame state + any event.
    VadFrameResult ProcessCaptureFrame(const std::int16_t* samples,
                                       std::size_t sampleCount,
                                       std::vector<std::int16_t>& cleanedOut);

    // Tell the detector that playback just stopped — starts cooldown to suppress
    // tail-echo false positives.
    void BeginPlaybackCooldown();

    // While true, all VAD positives are suppressed (no SpeechStarted events).
    // Use this whenever the speaker is actively playing to prevent self-barge-in
    // from speaker audio leaking through software AEC.
    void SetPlaybackActive(bool active) noexcept { playbackActive_ = active; }

    // Reset utterance state (after consuming a finished utterance or aborting one).
    void ResetUtterance();

    int SampleRate() const noexcept { return config_.sampleRate; }
    std::size_t FrameSamples() const noexcept { return frameSamples_; }
    bool IsSpeechActive() const noexcept { return inUtterance_; }

private:
    VadConfig config_;
    std::size_t frameSamples_;
    EchoCanceller aec_;
    VadDetector vad_;

    int warmupFramesRemaining_;
    int playbackCooldownFrames_;
    int startSpeechFramesRequired_;
    int endSilenceFramesRequired_;
    std::size_t preRollFramesCapacity_;

    int consecutiveSpeechFrames_ = 0;
    int consecutiveSilenceFrames_ = 0;
    bool inUtterance_ = false;
    bool playbackActive_ = false;

    float noiseFloorRms_;
    float rmsAccum_ = 0.0f;
    int rmsCount_ = 0;
    float speakerRmsEma_ = 0.0f;

    std::deque<std::vector<std::int16_t>> preRoll_;
};

}  // namespace voice_agent
