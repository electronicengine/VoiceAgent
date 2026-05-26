#pragma once

#include "common/HttpClient.h"
#include "config/AppConfig.h"
#include "interpreter/IInterpreter.h"

#include <nlohmann/json.hpp>

#include <string>

namespace voice_agent {

class OpenAiInterpreter final : public IInterpreter {
public:
    explicit OpenAiInterpreter(const AppConfig& config);

    void ResetSession(std::string systemPrompt) override;
    InterpreterResponse Interpret(
        const std::string& userText,
        const InterpreterStreamCallback& onPartialResponse) override;

private:
    struct StreamParseState {
        std::string sseBuffer;
        std::string rawResponse;
        std::string fenceBuffer;
        std::string pendingSpeech;
        std::string inlineJsonBuffer;
        bool rootJsonMode = false;
        bool responseModeResolved = false;
        bool inFence = false;
        bool inInlineJson = false;
        int inlineJsonDepth = 0;
        bool inlineJsonInString = false;
        bool inlineJsonEscape = false;
    };

    std::vector<std::string> DefaultHeaders() const;
    std::string CreateAssistant(const std::string& instructions) const;
    std::string CreateThread() const;
    void AddUserMessageToThread(const std::string& userText) const;
    void CreateRun(StreamParseState& state, const InterpreterStreamCallback& onPartialResponse) const;
    std::string ExtractMessageText(const nlohmann::json& messageJson) const;
    InterpreterResponse BuildStructuredResponse(const std::string& rawText) const;
    void ConsumeStreamingChunk(
        const std::string& chunk,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
    void ConsumeSseEvent(
        const std::string& eventName,
        const std::string& payload,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
    void ConsumeStreamDelta(
        const std::string& text,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
    std::string ExtractTextDelta(const nlohmann::json& eventJson, const std::string& eventName) const;
    void ConsumeResponseText(
        const std::string& text,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
    void EmitCompletedSpeech(
        std::string& pendingSpeech,
        bool flushAll,
        const InterpreterStreamCallback& onPartialResponse) const;

    AppConfig config_;
    HttpClient httpClient_;
    std::string assistantId_;
    std::string threadId_;
};

}  // namespace voice_agent