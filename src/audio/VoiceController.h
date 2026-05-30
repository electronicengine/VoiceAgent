#pragma once

#include "audio/IMicrophone.h"
#include "audio/ISpeaker.h"
#include "audio/VoiceActivityDetector.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace voice_agent {

// Thin façade that wires a microphone, a speaker, and a VAD/AEC together to:
//   * stream cleaned mic frames through AEC (with the speaker's outgoing PCM as
//     the reverse reference) + libfvad,
//   * notify the agent when an utterance starts (barge-in) and when it ends,
//   * play synthesized speech while *continuing* to listen, and
//   * cancel ongoing speech the moment the user starts a new utterance.
//
// The microphone capture thread drives the detector. The speaker worker thread
// pushes outgoing frames to the AEC reverse stream just before they hit ALSA.
// A single mutex serializes detector access between those two threads.
class VoiceController final {
public:
    using OnUtterance = std::function<void(std::vector<char> wavBytes)>;
    using OnBargeIn = std::function<void()>;

    VoiceController(std::unique_ptr<IMicrophone> microphone,
                    std::unique_ptr<ISpeaker> speaker,
                    const VadConfig& vadConfig);
    ~VoiceController();

    VoiceController(const VoiceController&) = delete;
    VoiceController& operator=(const VoiceController&) = delete;

    void Start();
    void Stop();

    void SetOnUtterance(OnUtterance cb);
    void SetOnBargeIn(OnBargeIn cb);

    // Mark whether the agent is currently "busy" (interpreting / speaking).
    // While busy, a SpeechStarted event triggers the OnBargeIn callback.
    void SetBusy(bool busy);

    // Speech output: enqueue PCM (mono S16LE @ sample rate) for playback.
    void Speak(std::string pcm);

    // Block until current playback queue drains (or is aborted).
    void WaitUntilSpeakerIdle();

    // Stop active playback immediately and start a playback cooldown in the
    // detector to suppress trailing-echo false positives.
    void StopSpeaking();

    bool IsSpeakerActive() const { return speaker_->IsActive(); }
    int SampleRate() const { return microphone_->SampleRate(); }

private:
    void OnMicFrame(const std::int16_t* samples, std::size_t sampleCount);
    void OnSpeakerFrame(const std::int16_t* samples, std::size_t sampleCount);
    void EmitUtterance();

    std::unique_ptr<IMicrophone> microphone_;
    std::unique_ptr<ISpeaker> speaker_;
    VoiceActivityDetector detector_;

    std::mutex mutex_;          // serializes detector_ access
    std::mutex callbackMutex_;  // protects callbacks
    OnUtterance onUtterance_;
    OnBargeIn onBargeIn_;

    std::vector<std::int16_t> currentUtterance_;
    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    bool speakerWasActive_ = false;
};

}  // namespace voice_agent
