#pragma once

#include "common/CancellationToken.h"
#include "common/HttpClient.h"
#include "config/AppConfig.h"
#include "synthesizer/ISynthesizer.h"

#include <string>

namespace voice_agent {

class AzureRestSynthesizer final : public ISynthesizer {
public:
    explicit AzureRestSynthesizer(const AppConfig& config);

    std::string Synthesize(const InterpreterResponse& response,
                           const CancellationToken* token = nullptr) const override;

private:
    std::string RequestPcmAudio(const std::string& text, const CancellationToken* token) const;

    AppConfig config_;
    HttpClient httpClient_;
};

}  // namespace voice_agent