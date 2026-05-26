#pragma once

#include "audio/ISpeaker.h"

#include <string>

namespace voice_agent {

class AlsaSpeaker final : public ISpeaker {
public:
    AlsaSpeaker(int sampleRate, std::string deviceName);
    void PlayPcm16KhzMono(const std::string& pcmAudio) const override;

private:
    int sampleRate_;
    std::string deviceName_;
};

}  // namespace voice_agent