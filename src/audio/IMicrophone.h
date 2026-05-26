#pragma once

#include <vector>

namespace voice_agent {

class IMicrophone {
public:
    virtual ~IMicrophone() = default;
    virtual std::vector<char> CaptureWavBytes() const = 0;
};

}  // namespace voice_agent