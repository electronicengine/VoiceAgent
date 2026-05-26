#include "transcriber/AzureTranscriber.h"

#include "audio/AlsaMicrophone.h"
#include "common/StringUtils.h"
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

namespace voice_agent {

    using json = nlohmann::json;

    AzureTranscriber::AzureTranscriber(const AppConfig& config)
        : AzureTranscriber(
            config,
            std::make_unique<AlsaMicrophone>(config)) {}

    AzureTranscriber::AzureTranscriber(const AppConfig& config, std::unique_ptr<IMicrophone> microphone)
        : ITranscriber(config, std::move(microphone)) {}

    HttpRequest AzureTranscriber::BuildRequest(const std::vector<char>& audioData) const {
        const AppConfig& config = Config();
        HttpRequest request;
        request.url =
            "https://" + config.azureSpeechRegion +
            ".stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1?language=" +
            config.speechLanguage + "&format=detailed";
        request.headers = {
            "Ocp-Apim-Subscription-Key: " + config.azureSpeechKey,
            "Content-Type: audio/wav; codecs=audio/pcm; samplerate=" + std::to_string(config.speechSampleRate),
            "Accept: application/json",
        };
        request.body.assign(audioData.begin(), audioData.end());
        return request;
    }

    std::string AzureTranscriber::ExtractTranscript(const json& responseJson) const {
        if (responseJson.contains("DisplayText") && responseJson.at("DisplayText").is_string()) {
            return Trim(responseJson.at("DisplayText").get<std::string>());
        }
        if (responseJson.contains("NBest") && responseJson.at("NBest").is_array() && !responseJson.at("NBest").empty()) {
            const auto& best = responseJson.at("NBest").front();
            if (best.contains("Display") && best.at("Display").is_string()) {
                return Trim(best.at("Display").get<std::string>());
            }
        }
        if (responseJson.contains("RecognitionStatus") && responseJson.at("RecognitionStatus") == "NoMatch") {
            return {};
        }

        throw std::runtime_error("Azure STT response did not include recognized text.");
    }

    std::string AzureTranscriber::ProviderName() const {
        return "Azure";
    }

}  // namespace voice_agent