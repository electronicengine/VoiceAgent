#include "transcriber/ITranscriber.h"

#include "common/StringUtils.h"

#include <stdexcept>
#include <utility>

namespace voice_agent {

ITranscriber::ITranscriber(const AppConfig& config, std::unique_ptr<IMicrophone> microphone)
    : config_(config),
      microphone_(std::move(microphone)) {}

std::string ITranscriber::ListenOnce() const {
    const std::vector<char> audioData = microphone_->CaptureWavBytes();
    if (audioData.empty()) {
        return {};
    }

    return RecognizeFromAudio(audioData);
}

const AppConfig& ITranscriber::Config() const {
    return config_;
}

std::string ITranscriber::RecognizeFromAudio(const std::vector<char>& audioData) const {
    const HttpResponse response = httpClient_.Post(BuildRequest(audioData));
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

}  // namespace voice_agent