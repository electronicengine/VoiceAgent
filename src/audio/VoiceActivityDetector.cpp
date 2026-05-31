#include "audio/VoiceActivityDetector.h"

#include "audio/VadDetector.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace voice_agent {

namespace {

constexpr float kMinRms = 250.0f;
constexpr float kNoiseFloorMultiplier = 3.0f;
// While our own speaker (or a known external producer) is playing, AEC residue
// of the agent's own TTS leaks through at RMS levels around 1500-2000. To keep
// real barge-in working we deliberately keep the gate above that residue, but
// well below typical user speech RMS (~3000-8000 on a desk mic). These values
// were tuned against an observed false-positive at rms=1886 / noise=37.
constexpr float kPlaybackRmsMultiplier = 8.0f;
constexpr float kPlaybackMinRms = 2500.0f;
constexpr int kPlaybackStartSpeechMultiplier = 2;  // need 2x normal consecutive frames

bool VadDebugEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("VOICE_VAD_DEBUG");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return enabled;
}

int FramesFromMs(int sampleRate, int ms) {
    return std::max(1, (sampleRate / 1000) * ms / 10);  // counted in 10ms frames
}

}  // namespace

VoiceActivityDetector::VoiceActivityDetector(const VadConfig& config)
    : config_(config),
      frameSamples_(static_cast<std::size_t>(config.sampleRate) * 10 / 1000),
      aec_(config.sampleRate),
      vad_(config.sampleRate, config.vadAggressiveness),
      warmupFramesRemaining_(std::max(1, config.startupWarmupMs / 10)),
      playbackCooldownFrames_(0),
      startSpeechFramesRequired_(std::max(1, config.startSpeechMs / 10)),
      endSilenceFramesRequired_(std::max(1, config.endSilenceMs / 10)),
      preRollFramesCapacity_(static_cast<std::size_t>(std::max(0, config.preRollMs / 10))),
      noiseFloorRms_(kMinRms) {
    aec_.SetStreamDelayMs(config.aecStreamDelayMs);
    (void)FramesFromMs;
}

void VoiceActivityDetector::PushReverseFrame(const std::int16_t* samples, std::size_t sampleCount) {
    aec_.PushReverseFrame(samples, sampleCount);
    const float r = VadDetector::Rms(samples, sampleCount);
    // Track recent peak speaker RMS over ~1s sliding decay.
    speakerRmsEma_ = std::max(r, speakerRmsEma_ * 0.97f);
}

VadFrameResult VoiceActivityDetector::ProcessCaptureFrame(const std::int16_t* samples,
                                                          std::size_t sampleCount,
                                                          std::vector<std::int16_t>& cleanedOut) {
    cleanedOut.assign(samples, samples + sampleCount);
    aec_.ProcessCaptureFrame(cleanedOut.data(), cleanedOut.size());

    VadFrameResult result;
    result.rms = VadDetector::Rms(cleanedOut.data(), cleanedOut.size());
    const bool fvadSays = vad_.IsSpeech(cleanedOut.data(), cleanedOut.size());

    // Startup warmup: estimate the noise floor before any detection.
    if (warmupFramesRemaining_ > 0) {
        rmsAccum_ += result.rms;
        ++rmsCount_;
        --warmupFramesRemaining_;
        if (warmupFramesRemaining_ == 0 && rmsCount_ > 0) {
            noiseFloorRms_ = std::max(kMinRms, rmsAccum_ / static_cast<float>(rmsCount_));
        }
        result.noiseFloorRms = noiseFloorRms_;
        return result;
    }

    if (playbackCooldownFrames_ > 0) {
        --playbackCooldownFrames_;
    }
    const bool inCooldown = playbackCooldownFrames_ > 0;

    // Cooldown / playback suppression only matter BEFORE an utterance begins
    // (to avoid false-positive starts triggered by AEC residue). Once we're
    // already inside an utterance — typically a barge-in that cancelled the
    // agent's TTS — keep tracking the user's speech to its natural end so the
    // captured audio is complete and a new turn can be transcribed.
    const bool suppressForEcho = (playbackActive_ || inCooldown) && !inUtterance_;

    // While the speaker is playing or in cooldown, do not update the noise floor
    // (it would absorb echo) and use a stricter threshold so AEC residue does
    // not trigger false barge-ins.
    const bool suppressNoiseUpdate = playbackActive_ || inCooldown;

    const bool rawVadPositive = fvadSays && !(inCooldown && !inUtterance_);
    if (!rawVadPositive && !suppressNoiseUpdate) {
        noiseFloorRms_ = 0.98f * noiseFloorRms_ + 0.02f * result.rms;
    }
    float threshold = std::max(kMinRms, noiseFloorRms_ * kNoiseFloorMultiplier);
    if (suppressForEcho) {
        threshold = std::max({threshold,
                              kPlaybackMinRms,
                              noiseFloorRms_ * kPlaybackRmsMultiplier});
    } else if (!playbackActive_ && !inCooldown) {
        // Decay the speaker RMS estimate when not playing.
        speakerRmsEma_ *= 0.9f;
    }
    const bool speechFrame = rawVadPositive && (result.rms >= threshold);

    if (VadDebugEnabled() && (fvadSays || speechFrame)) {
        std::fprintf(stderr,
                     "[vad] rms=%.0f noise=%.0f thr=%.0f spkRms=%.0f fvad=%d play=%d cool=%d -> speech=%d\n",
                     result.rms, noiseFloorRms_, threshold, speakerRmsEma_,
                     fvadSays ? 1 : 0,
                     playbackActive_ ? 1 : 0,
                     inCooldown ? 1 : 0,
                     speechFrame ? 1 : 0);
    }

    result.noiseFloorRms = noiseFloorRms_;
    result.speechDetectedRaw = speechFrame;

    if (!inUtterance_) {
        // Maintain pre-roll ring of cleaned frames.
        if (preRollFramesCapacity_ > 0) {
            preRoll_.emplace_back(cleanedOut);
            while (preRoll_.size() > preRollFramesCapacity_) {
                preRoll_.pop_front();
            }
        }

        if (speechFrame) {
            ++consecutiveSpeechFrames_;
            const int required = (playbackActive_ || playbackCooldownFrames_ > 0)
                                     ? startSpeechFramesRequired_ * kPlaybackStartSpeechMultiplier
                                     : startSpeechFramesRequired_;
            if (consecutiveSpeechFrames_ >= required) {
                inUtterance_ = true;
                consecutiveSilenceFrames_ = 0;
                result.event = VadEvent::SpeechStarted;
                // Flatten preRoll_ deque into a single buffer.
                std::size_t total = 0;
                for (const auto& f : preRoll_) total += f.size();
                result.preRollPcm.reserve(total);
                for (auto& f : preRoll_) {
                    result.preRollPcm.insert(result.preRollPcm.end(), f.begin(), f.end());
                }
                preRoll_.clear();
            }
        } else {
            consecutiveSpeechFrames_ = 0;
        }
    } else {
        if (speechFrame) {
            consecutiveSilenceFrames_ = 0;
        } else {
            ++consecutiveSilenceFrames_;
            if (consecutiveSilenceFrames_ >= endSilenceFramesRequired_) {
                inUtterance_ = false;
                consecutiveSpeechFrames_ = 0;
                consecutiveSilenceFrames_ = 0;
                result.event = VadEvent::SpeechEnded;
            }
        }
    }

    result.speechActive = inUtterance_;
    return result;
}

void VoiceActivityDetector::BeginPlaybackCooldown() {
    playbackCooldownFrames_ = std::max(1, config_.playbackCooldownMs / 10);
    aec_.ClearReverseHistory();
}

void VoiceActivityDetector::ResetUtterance() {
    inUtterance_ = false;
    consecutiveSpeechFrames_ = 0;
    consecutiveSilenceFrames_ = 0;
    preRoll_.clear();
}

}  // namespace voice_agent
