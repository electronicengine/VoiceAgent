#include "transcriber/ITranscriber.h"

#include "common/StringUtils.h"

#include <stdexcept>
#include <utility>

namespace voice_agent {

ITranscriber::ITranscriber(const AppConfig& config)
    : config_(config) {}

std::string ITranscriber::Transcribe(const std::vector<char>& wavBytes,
                                     const CancellationToken* token) const {
    if (wavBytes.empty()) {
        return {};
    }
    HttpRequest request = BuildRequest(wavBytes);
    request.cancellationToken = token;
    const HttpResponse response = httpClient_.Post(request);
    if (response.cancelled) {
        return {};
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            ProviderName() + " STT request returned HTTP " + std::to_string(response.statusCode) + ": " +
            response.body
        );
    }

    nlohmann::json responseJson;
    try {
        responseJson = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception& ex) {
        throw std::runtime_error(ProviderName() + " STT response was not valid JSON: " + std::string(ex.what()));
    }

    return Trim(ExtractTranscript(responseJson));
}

const AppConfig& ITranscriber::Config() const {
    return config_;
}

}  // namespace voice_agent