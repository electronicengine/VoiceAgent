#pragma once
#include <string>

namespace voice_agent {

class Operator {
public:
    Operator(const std::string& name) : name_(name) {}
    virtual ~Operator() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isReady() const noexcept = 0;

    const std::string& getName() const { return name_; }

protected:
    std::string name_;
};

} // namespace voice_agent
