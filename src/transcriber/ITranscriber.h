#pragma once

#include "audio/IMicrophone.h"
#include "common/HttpClient.h"
#include "config/AppConfig.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace voice_agent {

class ITranscriber {
public:
    virtual ~ITranscriber() = default;
    virtual std::string ListenOnce() const;

protected:
    ITranscriber(const AppConfig& config, std::unique_ptr<IMicrophone> microphone);

    const AppConfig& Config() const;

    virtual HttpRequest BuildRequest(const std::vector<char>& audioData) const = 0;
    virtual std::string ExtractTranscript(const nlohmann::json& responseJson) const = 0;
    virtual std::string ProviderName() const = 0;

private:
    std::string RecognizeFromAudio(const std::vector<char>& audioData) const;

    AppConfig config_;
    std::unique_ptr<IMicrophone> microphone_;
    HttpClient httpClient_;
};

}  // namespace voice_agent