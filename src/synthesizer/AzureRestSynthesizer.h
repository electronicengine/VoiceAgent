#pragma once

#include "audio/ISpeaker.h"
#include "common/HttpClient.h"
#include "config/AppConfig.h"
#include "synthesizer/ISynthesizer.h"

#include <memory>
#include <string>

namespace voice_agent {

class AzureRestSynthesizer final : public ISynthesizer {
public:
    explicit AzureRestSynthesizer(const AppConfig& config);
    AzureRestSynthesizer(const AppConfig& config, std::unique_ptr<ISpeaker> speaker);

    void Synthesize(const InterpreterResponse& response) const override;

private:
    std::string RequestPcmAudio(const std::string& text) const;

    AppConfig config_;
    std::unique_ptr<ISpeaker> speaker_;
    HttpClient httpClient_;
};

}  // namespace voice_agent