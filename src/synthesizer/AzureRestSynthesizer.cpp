#include "synthesizer/AzureRestSynthesizer.h"

#include "common/StringUtils.h"

#include <stdexcept>
#include <utility>

namespace voice_agent {

AzureRestSynthesizer::AzureRestSynthesizer(const AppConfig& config)
    : config_(config) {}

std::string AzureRestSynthesizer::Synthesize(const InterpreterResponse& response,
                                             const CancellationToken* token) const {
    const std::string speakableText = response.SpeakableText();
    if (speakableText.empty()) {
        return {};
    }
    if (token != nullptr && token->IsCancelled()) {
        return {};
    }
    return RequestPcmAudio(speakableText, token);
}

std::string AzureRestSynthesizer::RequestPcmAudio(const std::string& text,
                                                  const CancellationToken* token) const {
    const std::string ssml =
        "<speak version='1.0' xml:lang='" + config_.speechLanguage + "'>"
        "<voice xml:lang='" + config_.speechLanguage + "' name='" + config_.voiceName + "'>" +
        EscapeXml(text) + "</voice></speak>";

    HttpRequest request;
    request.url = "https://" + config_.azureSpeechRegion + ".tts.speech.microsoft.com/cognitiveservices/v1";
    request.headers = {
        "Ocp-Apim-Subscription-Key: " + config_.azureSpeechKey,
        "Content-Type: application/ssml+xml",
        "X-Microsoft-OutputFormat: raw-16khz-16bit-mono-pcm",
        "User-Agent: cpp-voice-agent",
    };
    request.body = ssml;
    request.cancellationToken = token;

    const HttpResponse response = httpClient_.Post(request);
    if (response.cancelled) {
        return {};
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "Azure TTS request returned HTTP " + std::to_string(response.statusCode) + ": " + response.body
        );
    }

    return response.body;
}

}  // namespace voice_agent