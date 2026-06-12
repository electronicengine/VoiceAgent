#include "interpretertest/GoogleAiInterpreterTest.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace voice_agent {

// ─────────────────────────────────────────────────────────────────────────────
// Session management tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, ResetSession_SetsSystemPromptAndClearsHistory) {
    TestableGoogleAiInterpreter interp(MakeConfig());

    interp.ResetSession("Sen bir asistansın.");

    EXPECT_EQ(interp.SystemPrompt(), "Sen bir asistansın.");
    EXPECT_TRUE(interp.History().empty());
}

TEST_F(GoogleAiInterpreterTest, ResetSession_CalledTwice_HistoryCleared) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.sseChunks = { MakeGeminiSseLine("Merhaba!") };

    interp.ResetSession("İlk prompt.");
    InterpreterInput input;
    input.text = "test";
    interp.Interpret(input, nullptr);

    // History should have user + model turns.
    ASSERT_EQ(interp.History().size(), 2u);

    // Second reset must wipe the history.
    interp.ResetSession("İkinci prompt.");
    EXPECT_EQ(interp.SystemPrompt(), "İkinci prompt.");
    EXPECT_TRUE(interp.History().empty());
}

TEST_F(GoogleAiInterpreterTest, Interpret_WithoutResetSession_Throws) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    InterpreterInput input;
    input.text = "merhaba";
    EXPECT_THROW(interp.Interpret(input, nullptr), std::runtime_error);
}

// ─────────────────────────────────────────────────────────────────────────────
// Conversation history tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, Interpret_SingleTurn_HistoryHasUserAndModelTurns) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem promptu.");
    interp.sseChunks = { MakeGeminiSseLine("Yanıt metni.") };

    InterpreterInput input;
    input.text = "Kullanıcı mesajı.";
    interp.Interpret(input, nullptr);

    ASSERT_EQ(interp.History().size(), 2u);

    const auto& userTurn = interp.History()[0];
    EXPECT_EQ(userTurn.at("role").get<std::string>(), "user");
    EXPECT_EQ(userTurn.at("parts")[0].at("text").get<std::string>(), "Kullanıcı mesajı.");

    const auto& modelTurn = interp.History()[1];
    EXPECT_EQ(modelTurn.at("role").get<std::string>(), "model");
    EXPECT_FALSE(modelTurn.at("parts")[0].at("text").get<std::string>().empty());
}

TEST_F(GoogleAiInterpreterTest, Interpret_MultiTurn_SecondCallIncludesPreviousHistory) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    // First turn
    interp.sseChunks = { MakeGeminiSseLine("Birinci yanıt.") };
    InterpreterInput input1;
    input1.text = "Birinci soru.";
    interp.Interpret(input1, nullptr);

    ASSERT_EQ(interp.History().size(), 2u);

    // Second turn
    interp.sseChunks = { MakeGeminiSseLine("İkinci yanıt.") };
    InterpreterInput input2;
    input2.text = "İkinci soru.";
    interp.Interpret(input2, nullptr);

    // user1, model1, user2, model2
    ASSERT_EQ(interp.History().size(), 4u);
    EXPECT_EQ(interp.History()[0].at("role").get<std::string>(), "user");
    EXPECT_EQ(interp.History()[1].at("role").get<std::string>(), "model");
    EXPECT_EQ(interp.History()[2].at("role").get<std::string>(), "user");
    EXPECT_EQ(interp.History()[3].at("role").get<std::string>(), "model");
    EXPECT_EQ(interp.History()[2].at("parts")[0].at("text").get<std::string>(), "İkinci soru.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cancellation tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, Interpret_TokenCancelledBeforeStart_HistoryNotUpdated) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");
    // Chunks that would cancel mid-stream (subclass checks before each chunk)
    interp.sseChunks = { MakeGeminiSseLine("Bu gelmemeli.") };

    CancellationToken token;
    token.Cancel();  // already cancelled

    InterpreterInput input;
    input.text = "İptal edilmiş istek.";
    const auto response = interp.Interpret(input, nullptr, &token);

    // Response should be empty and history should NOT have a model turn appended.
    EXPECT_TRUE(response.Empty());
    // User turn was added but model turn was NOT (cancelled before stream).
    ASSERT_EQ(interp.History().size(), 1u);
    EXPECT_EQ(interp.History()[0].at("role").get<std::string>(), "user");
}

// ─────────────────────────────────────────────────────────────────────────────
// SSE stream parsing tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, Interpret_SseChunkSplitAcrossPackets_ReconstructsFullText) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    // Build the full SSE event and split it into two arbitrary byte boundaries.
    const std::string fullLine = MakeGeminiSseLine("Parçalı veri.");
    const std::size_t splitAt = fullLine.size() / 2;
    interp.sseChunks = {
        fullLine.substr(0, splitAt),
        fullLine.substr(splitAt)
    };

    InterpreterInput input;
    input.text = "Soru.";
    const auto response = interp.Interpret(input, nullptr);

    ASSERT_FALSE(response.Empty());
    EXPECT_NE(response.DisplayText().find("Parçalı veri."), std::string::npos);
}

TEST_F(GoogleAiInterpreterTest, Interpret_MultipleDeltaChunks_ConcatenatesText) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    interp.sseChunks = {
        MakeGeminiSseLine("Birinci "),
        MakeGeminiSseLine("ikinci "),
        MakeGeminiSseLine("üçüncü.")
    };

    InterpreterInput input;
    input.text = "Soru.";
    const auto response = interp.Interpret(input, nullptr);

    const std::string text = response.DisplayText();
    EXPECT_NE(text.find("Birinci"), std::string::npos);
    EXPECT_NE(text.find("ikinci"), std::string::npos);
    EXPECT_NE(text.find("üçüncü."), std::string::npos);
}

TEST_F(GoogleAiInterpreterTest, ConsumeStreamingChunk_MalformedJson_Ignored) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    TestableGoogleAiInterpreter::State state;
    interp.FeedChunk("data: {not valid json}\n\n", state, nullptr);

    // No crash, no text accumulated.
    EXPECT_TRUE(state.rawResponse.empty());
}

TEST_F(GoogleAiInterpreterTest, ConsumeStreamingChunk_MissingCandidates_Ignored) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    TestableGoogleAiInterpreter::State state;
    interp.FeedChunk("data: {\"usageMetadata\": {}}\n\n", state, nullptr);

    EXPECT_TRUE(state.rawResponse.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Streaming callback tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, Interpret_StreamCallbackCalledWithSpeechSegments) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    // Three sentences — each ends with a sentence terminator so the callback
    // should be triggered at least once before the final flush.
    interp.sseChunks = {
        MakeGeminiSseLine("Merhaba. Nasılsın? İyiyim.")
    };

    std::vector<InterpreterResponse> partials;
    InterpreterInput input;
    input.text = "Soru.";
    const auto finalResponse = interp.Interpret(input, [&](const InterpreterResponse& r) {
        partials.push_back(r);
    });

    // At least one partial (or the final flush) should carry speech text.
    EXPECT_FALSE(partials.empty());
    bool hasSpeech = false;
    for (const auto& p : partials) {
        for (const auto& seg : p.segments) {
            if (seg.type == ResponseSegmentType::Speech && !seg.content.empty()) {
                hasSpeech = true;
            }
        }
    }
    EXPECT_TRUE(hasSpeech);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildStructuredResponse tests (via ParseResponse helper)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_PlainText_ReturnsSpeechSegment) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const auto response = interp.ParseResponse("Merhaba, nasıl yardımcı olabilirim?");

    ASSERT_FALSE(response.segments.empty());
    EXPECT_EQ(response.segments[0].type, ResponseSegmentType::Speech);
    EXPECT_TRUE(response.segments[0].speakable);
}

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_RootJsonObject_ReturnsJsonSegment) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const auto response = interp.ParseResponse(R"({"tool":"google","arguments":{}})");

    ASSERT_EQ(response.segments.size(), 1u);
    EXPECT_EQ(response.segments[0].type, ResponseSegmentType::Json);
    EXPECT_FALSE(response.segments[0].speakable);
}

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_CodeFence_ReturnsCodeSegment) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const std::string raw =
        "İşte kod:\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n";
    const auto response = interp.ParseResponse(raw);

    bool foundCode = false;
    for (const auto& seg : response.segments) {
        if (seg.type == ResponseSegmentType::Code) {
            foundCode = true;
        }
    }
    EXPECT_TRUE(foundCode);
}

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_BashFence_ReturnsCommandSegment) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const std::string raw =
        "Komutu çalıştır:\n"
        "```bash\n"
        "ls -la\n"
        "```\n";
    const auto response = interp.ParseResponse(raw);

    bool foundCommand = false;
    for (const auto& seg : response.segments) {
        if (seg.type == ResponseSegmentType::Command) {
            foundCommand = true;
        }
    }
    EXPECT_TRUE(foundCommand);
}

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_MixedTextAndCode_HasBothSegments) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const std::string raw =
        "Açıklama.\n"
        "```python\n"
        "print('hello')\n"
        "```\n"
        "Devam metni.";
    const auto response = interp.ParseResponse(raw);

    bool hasSpeech = false;
    bool hasCode = false;
    for (const auto& seg : response.segments) {
        if (seg.type == ResponseSegmentType::Speech) hasSpeech = true;
        if (seg.type == ResponseSegmentType::Code)   hasCode   = true;
    }
    EXPECT_TRUE(hasSpeech);
    EXPECT_TRUE(hasCode);
}

TEST_F(GoogleAiInterpreterTest, BuildStructuredResponse_EmptyInput_ReturnsEmptyResponse) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    const auto response = interp.ParseResponse("");
    EXPECT_TRUE(response.Empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline tool-call JSON in stream
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(GoogleAiInterpreterTest, Interpret_InlineToolCallJson_ProducesJsonSegment) {
    TestableGoogleAiInterpreter interp(MakeConfig());
    interp.ResetSession("Sistem.");

    // Model outputs a bare tool-call JSON inline with surrounding speech.
    const std::string toolCall = R"({"tool":"myTool","arguments":{"key":"value"}})";
    interp.sseChunks = { MakeGeminiSseLine("Çağrı: " + toolCall + " tamam.") };

    InterpreterInput input;
    input.text = "Bir şey yap.";
    const auto response = interp.Interpret(input, nullptr);

    bool hasJson = false;
    for (const auto& seg : response.segments) {
        if (seg.type == ResponseSegmentType::Json) {
            hasJson = true;
        }
    }
    EXPECT_TRUE(hasJson);
}

} // namespace voice_agent
