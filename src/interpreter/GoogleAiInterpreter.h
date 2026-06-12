#pragma once

#include "common/HttpClient.h"
#include "config/AppConfig.h"
#include "interpreter/IInterpreter.h"
#include "interpreter/InterpreterTypes.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace voice_agent {

class GoogleAiInterpreter : public IInterpreter {
public:
    explicit GoogleAiInterpreter(const AppConfig& config);

    void ResetSession(std::string systemPrompt) override;
    InterpreterResponse Interpret(
        const InterpreterInput& input,
        const InterpreterStreamCallback& onPartialResponse,
        const CancellationToken* token = nullptr) override;

protected:
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
        const CancellationToken* cancellationToken = nullptr;
    };

    // Virtual so tests can override to inject fake SSE without real HTTP.
    virtual void RunStreamRequest(
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;

    void ConsumeStreamingChunk(
        const std::string& chunk,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;

    InterpreterResponse BuildStructuredResponse(const std::string& rawText) const;

    std::string systemPrompt_;
    std::vector<nlohmann::json> conversationHistory_;

private:
    std::vector<std::string> DefaultHeaders() const;
    nlohmann::json BuildPartsFromInput(const InterpreterInput& input) const;
    nlohmann::json BuildRequestBody() const;
    void ConsumeSseEvent(
        const std::string& payload,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
    void ConsumeStreamDelta(
        const std::string& text,
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const;
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
};

}  // namespace voice_agent
