#pragma once

#include "config/AppConfig.h"
#include "transcriber/ITranscriber.h"

#include <string>
#include <vector>

namespace voice_agent {

class DeepgramTranscriber final : public ITranscriber {
public:
    explicit DeepgramTranscriber(const AppConfig& config);

private:
    HttpRequest BuildRequest(const std::vector<char>& audioData) const override;
    std::string ExtractTranscript(const nlohmann::json& responseJson) const override;
    std::string ProviderName() const override;
};

}  // namespace voice_agent