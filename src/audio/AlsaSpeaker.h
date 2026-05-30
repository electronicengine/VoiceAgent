#pragma once

#include "audio/ISpeaker.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace voice_agent {

class AlsaSpeaker final : public ISpeaker {
public:
    AlsaSpeaker(int sampleRate, std::string deviceName);
    ~AlsaSpeaker() override;

    AlsaSpeaker(const AlsaSpeaker&) = delete;
    AlsaSpeaker& operator=(const AlsaSpeaker&) = delete;

    void Start() override;
    void Shutdown() override;
    void Enqueue(std::string pcm) override;
    void StopPlayback() override;
    void WaitUntilIdle() override;
    bool IsActive() const override;
    void SetFrameSink(FrameSink sink) override;
    int SampleRate() const override { return sampleRate_; }

private:
    void WorkerLoop();

    int sampleRate_;
    std::string deviceName_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::deque<std::string> queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> abortCurrent_{false};
    std::atomic<bool> writing_{false};
    FrameSink frameSink_;
    std::thread thread_;
};

}  // namespace voice_agent