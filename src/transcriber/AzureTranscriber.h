#pragma once

#include "audio/IMicrophone.h"
#include "config/AppConfig.h"
#include "transcriber/ITranscriber.h"

#include <memory>
#include <string>
#include <vector>

namespace voice_agent {

class AzureTranscriber final : public ITranscriber {
public:
    explicit AzureTranscriber(const AppConfig& config);
    AzureTranscriber(const AppConfig& config, std::unique_ptr<IMicrophone> microphone);

private:
    HttpRequest BuildRequest(const std::vector<char>& audioData) const override;
    std::string ExtractTranscript(const nlohmann::json& responseJson) const override;
    std::string ProviderName() const override;
};

}  // namespace voice_agent