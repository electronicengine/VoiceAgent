#pragma once

#include "audio/IMicrophone.h"
#include "config/AppConfig.h"

#include <atomic>
#include <string>
#include <thread>

namespace voice_agent {

class AlsaMicrophone final : public IMicrophone {
public:
    explicit AlsaMicrophone(const AppConfig& config);
    ~AlsaMicrophone() override;

    AlsaMicrophone(const AlsaMicrophone&) = delete;
    AlsaMicrophone& operator=(const AlsaMicrophone&) = delete;

    void Start(FrameCallback callback) override;
    void Stop() override;
    int SampleRate() const override { return sampleRate_; }

private:
    void CaptureLoop();

    int sampleRate_;
    std::string deviceName_;
    FrameCallback callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace voice_agent