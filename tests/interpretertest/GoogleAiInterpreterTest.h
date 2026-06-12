#pragma once

#include "interpreter/GoogleAiInterpreter.h"
#include "config/AppConfig.h"
#include "common/CancellationToken.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace voice_agent {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a valid Gemini SSE data line from a text delta.
// ─────────────────────────────────────────────────────────────────────────────
inline std::string MakeGeminiSseLine(const std::string& text, bool finished = false) {
    nlohmann::json payload;
    payload["candidates"] = nlohmann::json::array();
    nlohmann::json candidate;
    candidate["content"]["role"] = "model";
    candidate["content"]["parts"] = nlohmann::json::array();
    candidate["content"]["parts"].push_back({{"text", text}});
    if (finished) {
        candidate["finishReason"] = "STOP";
    }
    payload["candidates"].push_back(candidate);
    // Wrap as SSE: "data: <json>\n\n"
    return "data: " + payload.dump() + "\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Testable subclass: overrides RunStreamRequest to feed fake SSE chunks.
// ─────────────────────────────────────────────────────────────────────────────
class TestableGoogleAiInterpreter : public GoogleAiInterpreter {
public:
    explicit TestableGoogleAiInterpreter(const AppConfig& config)
        : GoogleAiInterpreter(config) {}

    // Populate before calling Interpret().
    std::vector<std::string> sseChunks;

    // Expose internals for assertions.
    const std::vector<nlohmann::json>& History() const { return conversationHistory_; }
    const std::string& SystemPrompt() const { return systemPrompt_; }

    // Re-export StreamParseState so test code can instantiate it directly.
    using State = StreamParseState;

    // Expose chunk consumer so it can be tested directly.
    void FeedChunk(
        const std::string& chunk,
        StreamParseState& state,
        const InterpreterStreamCallback& cb) const {
        ConsumeStreamingChunk(chunk, state, cb);
    }

    // Expose response builder so it can be tested directly.
    InterpreterResponse ParseResponse(const std::string& raw) const {
        return BuildStructuredResponse(raw);
    }

protected:
    void RunStreamRequest(
        StreamParseState& state,
        const InterpreterStreamCallback& onPartialResponse) const override {
        for (const auto& chunk : sseChunks) {
            if (state.cancellationToken != nullptr && state.cancellationToken->IsCancelled()) {
                return;
            }
            ConsumeStreamingChunk(chunk, state, onPartialResponse);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class GoogleAiInterpreterTest : public ::testing::Test {
protected:
    AppConfig MakeConfig() const {
        AppConfig cfg;
        cfg.googleAiApiKey  = "";
        cfg.googleAiBaseUrl = "https://generativelanguage.googleapis.com/v1beta";
        cfg.googleAiModel   = "gemini-2.0-flash";
        return cfg;
    }
};

} // namespace voice_agent
