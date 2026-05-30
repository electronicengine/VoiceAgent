#pragma once

#include "common/CancellationToken.h"
#include "common/HttpClient.h"
#include "config/AppConfig.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace voice_agent {

class ITranscriber {
public:
    virtual ~ITranscriber() = default;

    // Transcribe a WAV blob (mono S16LE @ Config().speechSampleRate).
    // Honors `token` if provided. Returns empty string if cancelled or no match.
    std::string Transcribe(const std::vector<char>& wavBytes,
                           const CancellationToken* token = nullptr) const;

protected:
    explicit ITranscriber(const AppConfig& config);

    const AppConfig& Config() const;

    virtual HttpRequest BuildRequest(const std::vector<char>& audioData) const = 0;
    virtual std::string ExtractTranscript(const nlohmann::json& responseJson) const = 0;
    virtual std::string ProviderName() const = 0;

private:
    AppConfig config_;
    HttpClient httpClient_;
};

}  // namespace voice_agent