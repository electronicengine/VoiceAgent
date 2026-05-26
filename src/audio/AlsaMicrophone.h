#pragma once

#include "audio/IMicrophone.h"
#include "config/AppConfig.h"

#include <string>
#include <vector>

namespace voice_agent {

class AlsaMicrophone final : public IMicrophone {
public:
    explicit AlsaMicrophone(const AppConfig& config);
    std::vector<char> CaptureWavBytes() const override;

private:
    int sampleRate_;
    int captureDurationSeconds_;
    bool vadEnabled_;
    int vadFrameMs_;
    int vadStartSpeechMs_;
    int vadEndSilenceMs_;
    int vadMaxCaptureMs_;
    int vadPreRollMs_;
    int vadAmplitudeThreshold_;
    std::string deviceName_;
};

}  // namespace voice_agent