#include "audio/VoiceController.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

namespace voice_agent {

namespace {

void AppendLE16(std::vector<char>& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void AppendLE32(std::vector<char>& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::vector<char> BuildWav(const std::vector<std::int16_t>& pcm, int sampleRate) {
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bps = 16;
    constexpr std::uint16_t blockAlign = channels * (bps / 8);
    const std::uint32_t dataSize = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * blockAlign;

    std::vector<char> wav;
    wav.reserve(44 + dataSize);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    AppendLE32(wav, 36u + dataSize);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    AppendLE32(wav, 16);
    AppendLE16(wav, 1);
    AppendLE16(wav, channels);
    AppendLE32(wav, static_cast<std::uint32_t>(sampleRate));
    AppendLE32(wav, byteRate);
    AppendLE16(wav, blockAlign);
    AppendLE16(wav, bps);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    AppendLE32(wav, dataSize);
    const auto* bytes = reinterpret_cast<const char*>(pcm.data());
    wav.insert(wav.end(), bytes, bytes + dataSize);
    return wav;
}

}  // namespace

VoiceController::VoiceController(std::unique_ptr<IMicrophone> microphone,
                                 std::unique_ptr<ISpeaker> speaker,
                                 const VadConfig& vadConfig)
    : microphone_(std::move(microphone)),
      speaker_(std::move(speaker)),
      detector_(vadConfig) {}

VoiceController::~VoiceController() {
    Stop();
}

void VoiceController::Start() {
    if (running_.exchange(true)) {
        return;
    }
    speaker_->Start();
    speaker_->SetFrameSink([this](const std::int16_t* s, std::size_t n) { OnSpeakerFrame(s, n); });
    microphone_->Start([this](const std::int16_t* s, std::size_t n) { OnMicFrame(s, n); });
    externalPollerStop_.store(false);
    externalPollerThread_ = std::thread([this]() { RunExternalPlaybackPoller(); });
}

void VoiceController::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    externalPollerStop_.store(true);
    if (externalPollerThread_.joinable()) {
        externalPollerThread_.join();
    }
    microphone_->Stop();
    speaker_->StopPlayback();
    speaker_->Shutdown();
}

void VoiceController::SetOnUtterance(OnUtterance cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onUtterance_ = std::move(cb);
}

void VoiceController::SetOnBargeIn(OnBargeIn cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onBargeIn_ = std::move(cb);
}

void VoiceController::SetBusy(bool busy) {
    busy_.store(busy);
}

void VoiceController::Speak(std::string pcm) {
    speaker_->Enqueue(std::move(pcm));
}

void VoiceController::WaitUntilSpeakerIdle() {
    speaker_->WaitUntilIdle();
}

void VoiceController::StopSpeaking() {
    speaker_->StopPlayback();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        detector_.BeginPlaybackCooldown();
    }
}

void VoiceController::OnSpeakerFrame(const std::int16_t* samples, std::size_t sampleCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    detector_.PushReverseFrame(samples, sampleCount);
}

void VoiceController::OnMicFrame(const std::int16_t* samples, std::size_t sampleCount) {
    VadFrameResult res;
    std::vector<std::int16_t> cleaned;
    bool startedNow = false;
    bool endedNow = false;
    bool speakerJustStopped = false;
    std::vector<std::int16_t> preRoll;
    const bool externalActive = externalPlaybackActive_.load();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Detect speaker stopping to start cooldown automatically.
        const bool speakerActive = speaker_->IsActive() || externalActive;
        detector_.SetPlaybackActive(speakerActive);
        if (speakerWasActive_ && !speakerActive) {
            detector_.BeginPlaybackCooldown();
            speakerJustStopped = true;
        }
        speakerWasActive_ = speakerActive;

        res = detector_.ProcessCaptureFrame(samples, sampleCount, cleaned);
        if (res.event == VadEvent::SpeechStarted) {
            startedNow = true;
            preRoll = std::move(res.preRollPcm);
            currentUtterance_.clear();
            currentUtterance_.reserve(preRoll.size() + sampleCount * 200);
            currentUtterance_.insert(currentUtterance_.end(), preRoll.begin(), preRoll.end());
            currentUtterance_.insert(currentUtterance_.end(), cleaned.begin(), cleaned.end());
        } else if (res.speechActive) {
            currentUtterance_.insert(currentUtterance_.end(), cleaned.begin(), cleaned.end());
        }
        if (res.event == VadEvent::SpeechEnded) {
            endedNow = true;
        }
    }

    if (speakerJustStopped) {
        // ok
    }

    if (startedNow) {
        const bool busyNow = busy_.load();
        std::cout << "[VoiceController] SpeechStarted rms=" << static_cast<int>(res.rms)
                  << " noise=" << static_cast<int>(res.noiseFloorRms)
                  << " speakerActive=" << (speakerWasActive_ ? 1 : 0)
                  << " externalPlayback=" << (externalActive ? 1 : 0)
                  << " busy=" << (busyNow ? 1 : 0) << "\n";
        OnBargeIn cb;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            cb = onBargeIn_;
        }
        // While an unsupervised audio source (e.g. mpv) is playing, the AEC has
        // no reference signal for it, so the captured frames are dominated by
        // music. Suppress barge-in to prevent the music from cancelling the
        // turn that just started it. The user can still issue a new command;
        // SpeechEnded -> OnUtterance -> HandleUtterance will cancel/replace
        // the current turn naturally.
        if (cb && busyNow && !externalActive) {
            std::cout << "[VoiceController] barge-in -> cancelling current turn\n";
            cb();
        } else if (externalActive) {
            std::cout << "[VoiceController] barge-in suppressed (external playback active)\n";
        }
    }

    if (endedNow) {
        EmitUtterance();
    }
}

void VoiceController::RunExternalPlaybackPoller() {
    // Polls every ~500 ms for known external audio producers (currently mpv,
    // started by the music skill via ShellTool). Such producers play through
    // PipeWire/Pulse directly, bypassing AlsaSpeaker, so the AEC has no
    // reference signal for them. While they are active we tell the VAD to
    // treat playback as on (stricter thresholds) and suppress barge-in.
    while (!externalPollerStop_.load()) {
        const int rc = std::system("pgrep -x mpv >/dev/null 2>&1");
        const bool active = (rc == 0);
        const bool prev = externalPlaybackActive_.exchange(active);
        if (active != prev) {
            std::cout << "[VoiceController] External playback (mpv) "
                      << (active ? "started" : "stopped") << "\n";
        }
        for (int i = 0; i < 5 && !externalPollerStop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void VoiceController::EmitUtterance() {
    std::vector<std::int16_t> utterance;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        utterance.swap(currentUtterance_);
        detector_.ResetUtterance();
    }
    if (utterance.empty()) {
        return;
    }
    std::vector<char> wav = BuildWav(utterance, microphone_->SampleRate());
    OnUtterance cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = onUtterance_;
    }
    if (cb) {
        cb(std::move(wav));
    }
}

bool VoiceController::CaptureNextUtterance(
    int timeoutMs,
    std::vector<char>& outWav,
    const CancellationToken* token
) {
    std::mutex captureMutex;
    std::condition_variable cv;
    bool received = false;
    std::vector<char> captured;

    OnUtterance previousOnUtterance;
    OnBargeIn previousOnBargeIn;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        previousOnUtterance = std::move(onUtterance_);
        previousOnBargeIn = std::move(onBargeIn_);

        onUtterance_ = [&](std::vector<char> wavBytes) {
            std::lock_guard<std::mutex> capLock(captureMutex);
            if (received) {
                return;
            }
            captured = std::move(wavBytes);
            received = true;
            cv.notify_all();
        };
        // Suppress barge-in while we wait so the user's spoken answer does not
        // cancel the turn that is currently running the WebBrowserTool.
        onBargeIn_ = []() {};
    }

    bool ok = false;
    {
        std::unique_lock<std::mutex> capLock(captureMutex);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(std::max(timeoutMs, 0));
        while (!received) {
            if (token != nullptr && token->IsCancelled()) {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            const auto pollDeadline = std::min(deadline,
                now + std::chrono::milliseconds(200));
            cv.wait_until(capLock, pollDeadline);
        }
        if (received) {
            outWav = std::move(captured);
            ok = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        onUtterance_ = std::move(previousOnUtterance);
        onBargeIn_ = std::move(previousOnBargeIn);
    }
    return ok;
}

}  // namespace voice_agent
