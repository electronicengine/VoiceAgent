#include "transcriber/DeepgramTranscriber.h"

#include "common/StringUtils.h"

#include <stdexcept>
#include <utility>

namespace voice_agent {

namespace {

std::string NormalizeDeepgramLanguage(std::string language) {
    language = Trim(std::move(language));
    if (language.empty()) {
        return {};
    }

    const std::size_t separator = language.find_first_of("-_");
    if (separator != std::string::npos) {
        language = language.substr(0, separator);
    }

    return ToLower(std::move(language));
}

std::string BoolQueryValue(bool value) {
    return value ? "true" : "false";
}

}  // namespace

DeepgramTranscriber::DeepgramTranscriber(const AppConfig& config)
    : ITranscriber(config) {}

HttpRequest DeepgramTranscriber::BuildRequest(const std::vector<char>& audioData) const {
    const AppConfig& config = Config();

    std::string url = config.deepgramBaseUrl;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    url += "/v1/listen?model=" + config.deepgramModel;
    url += "&smart_format=" + BoolQueryValue(config.deepgramSmartFormat);
    url += "&punctuate=" + BoolQueryValue(config.deepgramPunctuate);
    url += "&diarize=" + BoolQueryValue(config.deepgramDiarize);

    const std::string language = NormalizeDeepgramLanguage(config.speechLanguage);
    if (!language.empty()) {
        url += "&language=" + language;
    }

    HttpRequest request;
    request.url = std::move(url);
    request.headers = {
        "Authorization: Token " + config.deepgramApiKey,
        "Content-Type: audio/wav",
        "Accept: application/json",
    };
    request.body.assign(audioData.begin(), audioData.end());
    return request;
}

std::string DeepgramTranscriber::ExtractTranscript(const nlohmann::json& responseJson) const {
    if (!responseJson.contains("results") || !responseJson.at("results").is_object()) {
        throw std::runtime_error("Deepgram STT response did not include results.");
    }

    const auto& results = responseJson.at("results");
    if (!results.contains("channels") || !results.at("channels").is_array() || results.at("channels").empty()) {
        throw std::runtime_error("Deepgram STT response did not include channels.");
    }

    const auto& channel = results.at("channels").front();
    if (!channel.contains("alternatives") || !channel.at("alternatives").is_array() ||
        channel.at("alternatives").empty()) {
        throw std::runtime_error("Deepgram STT response did not include alternatives.");
    }

    const auto& best = channel.at("alternatives").front();
    if (best.contains("transcript") && best.at("transcript").is_string()) {
        return best.at("transcript").get<std::string>();
    }

    throw std::runtime_error("Deepgram STT response did not include transcript text.");
}

std::string DeepgramTranscriber::ProviderName() const {
    return "Deepgram";
}

}  // namespace voice_agent