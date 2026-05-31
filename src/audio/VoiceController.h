#pragma once

#include "audio/IMicrophone.h"
#include "audio/ISpeaker.h"
#include "audio/VoiceActivityDetector.h"
#include "common/CancellationToken.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

    // Block until the next complete utterance arrives, or until `timeoutMs`
    // elapses, or until `token` is cancelled. While waiting, the regular
    // OnUtterance / OnBargeIn callbacks are temporarily suppressed so the
    // captured speech does not feed back into the agent loop. Returns true
    // and fills `outWav` on success; returns false otherwise.
    bool CaptureNextUtterance(int timeoutMs,
                              std::vector<char>& outWav,
                              const CancellationToken* token);

    bool IsSpeakerActive() const { return speaker_->IsActive(); }
    int SampleRate() const { return microphone_->SampleRate(); }

private:
    void OnMicFrame(const std::int16_t* samples, std::size_t sampleCount);
    void OnSpeakerFrame(const std::int16_t* samples, std::size_t sampleCount);
    void EmitUtterance();
    void RunExternalPlaybackPoller();

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

    // True while a known external audio producer (currently: mpv) is playing
    // through the system default sink. The agent's AEC has no reference for
    // such audio, so VAD would otherwise treat the music itself as user
    // speech and trigger spurious barge-ins.
    std::atomic<bool> externalPlaybackActive_{false};
    std::atomic<bool> externalPollerStop_{false};
    std::thread externalPollerThread_;
};

}  // namespace voice_agent
