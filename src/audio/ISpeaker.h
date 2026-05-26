#pragma once

#include <string>

namespace voice_agent {

class ISpeaker {
public:
    virtual ~ISpeaker() = default;
    virtual void PlayPcm16KhzMono(const std::string& pcmAudio) const = 0;
};

}  // namespace voice_agent