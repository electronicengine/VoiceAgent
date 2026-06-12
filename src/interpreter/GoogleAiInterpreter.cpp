#include "interpreter/GoogleAiInterpreter.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cctype>
#include "common/logger.h"
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace voice_agent {

using json = nlohmann::json;

namespace {

ResponseSegmentType SegmentTypeFromFenceLanguage(const std::string& language) {
    const std::string lowered = ToLower(Trim(language));
    if (lowered == "json") {
        return ResponseSegmentType::Json;
    }
    if (lowered == "bash" || lowered == "sh" || lowered == "shell" || lowered == "command" || lowered == "cmd") {
        return ResponseSegmentType::Command;
    }
    if (!lowered.empty()) {
        return ResponseSegmentType::Code;
    }
    return ResponseSegmentType::Metadata;
}

bool IsSpeakable(ResponseSegmentType type) {
    return type == ResponseSegmentType::Speech;
}

void LogInterpreterMessage(const std::string& stage, const std::string& message) {
    //DEBUG("[GoogleAiInterpreter][{}] {}", stage, message);
    std::fflush(stdout);
}

std::string DetectMimeType(const std::string& filePath) {
    const std::string extension = ToLower(std::filesystem::path(filePath).extension().string());
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    if (extension == ".gif") {
        return "image/gif";
    }

    throw std::runtime_error("Unsupported image type for interpreter input: " + filePath);
}

std::string ReadFileAsBase64(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open image file: " + filePath);
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned char b0 = bytes[i];
        const unsigned char b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const unsigned char b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;

        result.push_back(table[b0 >> 2]);
        result.push_back(table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        result.push_back((i + 1 < bytes.size()) ? table[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=');
        result.push_back((i + 2 < bytes.size()) ? table[b2 & 0x3f] : '=');
    }

    return result;
}

bool IsToolCallPayload(const json& payload) {
    if (!payload.is_object()) {
        return false;
    }
    if (!payload.contains("tool") || !payload.at("tool").is_string()) {
        return false;
    }
    if (payload.contains("arguments") && !payload.at("arguments").is_object()) {
        return false;
    }
    if (payload.contains("parameters") && !payload.at("parameters").is_object()) {
        return false;
    }
    return true;
}

bool IsToolCallJsonText(const std::string& text) {
    const json payload = json::parse(text, nullptr, false);
    return !payload.is_discarded() && IsToolCallPayload(payload);
}

bool FindNextInlineToolJson(
    const std::string& text,
    std::size_t searchStart,
    std::size_t& jsonStart,
    std::size_t& jsonEnd) {
    for (std::size_t candidateStart = searchStart; candidateStart < text.size(); ++candidateStart) {
        if (text[candidateStart] != '{') {
            continue;
        }

        int depth = 0;
        bool inString = false;
        bool escape = false;
        for (std::size_t cursor = candidateStart; cursor < text.size(); ++cursor) {
            const char ch = text[cursor];
            if (inString) {
                if (escape) {
                    escape = false;
                    continue;
                }
                if (ch == '\\') {
                    escape = true;
                    continue;
                }
                if (ch == '"') {
                    inString = false;
                }
                continue;
            }

            if (ch == '"') {
                inString = true;
                continue;
            }
            if (ch == '{') {
                ++depth;
                continue;
            }
            if (ch != '}') {
                continue;
            }

            --depth;
            if (depth != 0) {
                continue;
            }

            const std::string candidate = text.substr(candidateStart, cursor - candidateStart + 1);
            if (IsToolCallJsonText(candidate)) {
                jsonStart = candidateStart;
                jsonEnd = cursor + 1;
                return true;
            }

            break;
        }
    }

    return false;
}

// Extracts all top-level JSON objects/arrays from a string that may contain
// multiple concatenated objects (e.g. Gemini thinking models send
// {candidates:[text]}{candidates:[thoughtSignature]} in a single SSE payload).
std::vector<json> ExtractJsonObjects(const std::string& payload) {
    std::vector<json> results;
    std::size_t pos = 0;
    const std::size_t len = payload.size();

    while (pos < len) {
        // Skip whitespace between objects.
        while (pos < len && std::isspace(static_cast<unsigned char>(payload[pos]))) {
            ++pos;
        }
        if (pos >= len) {
            break;
        }
        const char opener = payload[pos];
        if (opener != '{' && opener != '[') {
            break;
        }
        const char closer = (opener == '{') ? '}' : ']';

        int depth = 0;
        bool inString = false;
        bool escape = false;
        const std::size_t start = pos;
        bool completed = false;

        for (std::size_t i = pos; i < len; ++i) {
            const char ch = payload[i];
            if (inString) {
                if (escape) {
                    escape = false;
                    continue;
                }
                if (ch == '\\') {
                    escape = true;
                    continue;
                }
                if (ch == '"') {
                    inString = false;
                }
                continue;
            }
            if (ch == '"') {
                inString = true;
                continue;
            }
            if (ch == '{' || ch == '[') {
                ++depth;
                continue;
            }
            if (ch == '}' || ch == ']') {
                --depth;
                if (depth == 0) {
                    const std::string candidate = payload.substr(start, i - start + 1);
                    const json parsed = json::parse(candidate, nullptr, false);
                    if (!parsed.is_discarded()) {
                        results.push_back(parsed);
                    }
                    pos = i + 1;
                    completed = true;
                    break;
                }
            }
        }

        if (!completed) {
            break;  // Malformed remainder — stop.
        }
    }

    return results;
}

void AppendIfNotEmpty(InterpreterResponse& response, ResponseSegmentType type, const std::string& content) {
    const std::string trimmed = Trim(content);
    if (trimmed.empty()) {
        return;
    }

    response.segments.push_back(ResponseSegment{type, trimmed, IsSpeakable(type)});
}

void AppendParsedTextSegments(InterpreterResponse& response, const std::string& text) {
    std::size_t cursor = 0;
    std::size_t jsonStart = 0;
    std::size_t jsonEnd = 0;
    while (FindNextInlineToolJson(text, cursor, jsonStart, jsonEnd)) {
        AppendIfNotEmpty(response, ResponseSegmentType::Speech, text.substr(cursor, jsonStart - cursor));
        AppendIfNotEmpty(response, ResponseSegmentType::Json, text.substr(jsonStart, jsonEnd - jsonStart));
        cursor = jsonEnd;
    }

    AppendIfNotEmpty(response, ResponseSegmentType::Speech, text.substr(cursor));
}

}  // namespace

GoogleAiInterpreter::GoogleAiInterpreter(const AppConfig& config)
    : config_(config) {}

void GoogleAiInterpreter::ResetSession(std::string systemPrompt) {
    systemPrompt_ = std::move(systemPrompt);
    conversationHistory_.clear();
    LogInterpreterMessage("reset_session", "Session reset. System prompt loaded.");
}

InterpreterResponse GoogleAiInterpreter::Interpret(
    const InterpreterInput& input,
    const InterpreterStreamCallback& onPartialResponse,
    const CancellationToken* token) {
    if (systemPrompt_.empty()) {
        throw std::runtime_error("GoogleAI session is not initialized. Call ResetSession first.");
    }

    // Build user turn and append to conversation history
    json userTurn;
    userTurn["role"] = "user";
    userTurn["parts"] = BuildPartsFromInput(input);
    conversationHistory_.push_back(std::move(userTurn));

    StreamParseState state;
    state.cancellationToken = token;
    RunStreamRequest(state, onPartialResponse);

    if (token != nullptr && token->IsCancelled()) {
        // User turn stays in history; model turn is simply not appended.
        return InterpreterResponse{};
    }

    // Flush remaining buffers
    if (!state.sseBuffer.empty()) {
        ConsumeStreamingChunk("\n\n", state, onPartialResponse);
    }
    if (!state.fenceBuffer.empty() && !state.inFence) {
        state.pendingSpeech += state.fenceBuffer;
        state.fenceBuffer.clear();
    }
    if (!state.inlineJsonBuffer.empty()) {
        state.pendingSpeech += state.inlineJsonBuffer;
        state.inlineJsonBuffer.clear();
        state.inInlineJson = false;
        state.inlineJsonDepth = 0;
        state.inlineJsonInString = false;
        state.inlineJsonEscape = false;
    }
    EmitCompletedSpeech(state.pendingSpeech, true, onPartialResponse);

    if (token != nullptr && token->IsCancelled()) {
        // User turn stays; model turn not appended.
        return InterpreterResponse{};
    }

    // Append model response to conversation history for multi-turn
    if (!Trim(state.rawResponse).empty()) {
        json modelTurn;
        modelTurn["role"] = "model";
        modelTurn["parts"] = json::array();
        modelTurn["parts"].push_back({{"text", state.rawResponse}});
        conversationHistory_.push_back(std::move(modelTurn));
    }

    const InterpreterResponse finalResponse = BuildStructuredResponse(state.rawResponse);
    if (!finalResponse.Empty()) {
        return finalResponse;
    }

    LogInterpreterMessage("empty_response", "Streaming produced an empty response.");
    return InterpreterResponse{};
}

std::vector<std::string> GoogleAiInterpreter::DefaultHeaders() const {
    return {
        "Content-Type: application/json",
        "x-goog-api-key: " + config_.googleAiApiKey,
    };
}

nlohmann::json GoogleAiInterpreter::BuildPartsFromInput(const InterpreterInput& input) const {
    json parts = json::array();

    if (!Trim(input.text).empty()) {
        parts.push_back({{"text", input.text}});
    }

    for (const auto& image : input.images) {
        const std::filesystem::path path(image.filePath);
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Interpreter image file not found: " + image.filePath);
        }
        LogInterpreterMessage("build_parts", "Encoding image inline: " + path.string());
        parts.push_back({
            {"inline_data", {
                {"mime_type", DetectMimeType(image.filePath)},
                {"data", ReadFileAsBase64(image.filePath)}
            }}
        });
    }

    return parts;
}

nlohmann::json GoogleAiInterpreter::BuildRequestBody() const {
    json body;

    // System instruction
    if (!systemPrompt_.empty()) {
        body["system_instruction"] = {
            {"parts", json::array({{{"text", systemPrompt_}}})}
        };
    }

    // Conversation history (includes the latest user turn already appended)
    body["contents"] = conversationHistory_;

    // Generation config
    body["generationConfig"] = {
        {"temperature", 0.4}
    };

    return body;
}

void GoogleAiInterpreter::RunStreamRequest(
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    const std::string url =
        config_.googleAiBaseUrl +
        "/models/" + config_.googleAiModel +
        ":streamGenerateContent?alt=sse";

    const json requestBody = BuildRequestBody();

    HttpRequest request;
    request.url = url;
    request.headers = DefaultHeaders();
    request.body = requestBody.dump();
    request.cancellationToken = state.cancellationToken;

    const HttpResponse response = httpClient_.PostStream(
        request,
        [this, &state, &onPartialResponse](const std::string& chunk) {
            ConsumeStreamingChunk(chunk, state, onPartialResponse);
        }
    );

    if (response.cancelled) {
        return;
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "Google AI stream request failed with HTTP " +
            std::to_string(response.statusCode) + ": " + response.body
        );
    }
}

void GoogleAiInterpreter::ConsumeStreamingChunk(
    const std::string& chunk,
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    state.sseBuffer += chunk;

    std::size_t delimiterPos = 0;
    while ((delimiterPos = state.sseBuffer.find("\n\n")) != std::string::npos) {
        const std::string rawEvent = state.sseBuffer.substr(0, delimiterPos);
        state.sseBuffer.erase(0, delimiterPos + 2);

        if (rawEvent.empty()) {
            continue;
        }

        std::string payload;
        std::stringstream eventStream(rawEvent);
        std::string line;
        while (std::getline(eventStream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.size() >= 6 && line.compare(0, 6, "data: ") == 0) {
                if (!payload.empty()) {
                    payload += "\n";
                }
                payload += line.substr(6);
            }
        }

        if (!payload.empty()) {
            ConsumeSseEvent(payload, state, onPartialResponse);
        }
    }
}

void GoogleAiInterpreter::ConsumeSseEvent(
    const std::string& payload,
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    if (payload.empty() || payload == "[DONE]") {
        return;
    }

    // Gemini thinking models may concatenate multiple JSON objects in a single
    // SSE payload (e.g. text object + thoughtSignature object). Parse each one.
    const std::vector<json> events = ExtractJsonObjects(payload);
    if (events.empty()) {
        //DEBUG("[GoogleAiInterpreter][sse] Could not extract any JSON from payload.");
        return;
    }

    for (const auto& eventJson : events) {
        if (!eventJson.contains("candidates") || !eventJson.at("candidates").is_array()) {
            continue;
        }
        const auto& candidates = eventJson.at("candidates");
        if (candidates.empty()) {
            continue;
        }
        const auto& candidate = candidates[0];
        if (!candidate.is_object() || !candidate.contains("content") || !candidate.at("content").is_object()) {
            continue;
        }
        const auto& content = candidate.at("content");
        if (!content.contains("parts") || !content.at("parts").is_array()) {
            continue;
        }

        std::ostringstream combined;
        for (const auto& part : content.at("parts")) {
            if (!part.is_object()) {
                continue;
            }
            // Skip thought-signature parts (they carry no speakable text).
            if (part.contains("thoughtSignature")) {
                continue;
            }
            if (part.contains("text") && part.at("text").is_string()) {
                combined << part.at("text").get<std::string>();
            }
        }

        const std::string deltaText = combined.str();
        if (!deltaText.empty()) {
            ConsumeStreamDelta(deltaText, state, onPartialResponse);
        }
    }
}

void GoogleAiInterpreter::ConsumeStreamDelta(
    const std::string& text,
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    if (text.empty()) {
        return;
    }

    state.rawResponse += text;

    if (!state.responseModeResolved) {
        const std::string trimmed = Trim(state.rawResponse);
        if (!trimmed.empty()) {
            state.rootJsonMode = trimmed.front() == '{' || trimmed.front() == '[';
            state.responseModeResolved = true;
        }
    }

    if (state.rootJsonMode) {
        return;
    }

    ConsumeResponseText(text, state, onPartialResponse);
}

void GoogleAiInterpreter::ConsumeResponseText(
    const std::string& text,
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    const auto appendSpeakableText = [&state, &onPartialResponse, this](const std::string& fragment) {
        for (char speechChar : fragment) {
            state.pendingSpeech.push_back(speechChar);
            if (speechChar == '.' || speechChar == '!' || speechChar == '?' || speechChar == '\n') {
                EmitCompletedSpeech(state.pendingSpeech, false, onPartialResponse);
            }
        }
    };

    for (char ch : text) {
        if (state.inInlineJson) {
            state.inlineJsonBuffer.push_back(ch);

            if (state.inlineJsonInString) {
                if (state.inlineJsonEscape) {
                    state.inlineJsonEscape = false;
                    continue;
                }
                if (ch == '\\') {
                    state.inlineJsonEscape = true;
                    continue;
                }
                if (ch == '"') {
                    state.inlineJsonInString = false;
                }
                continue;
            }

            if (ch == '"') {
                state.inlineJsonInString = true;
                continue;
            }
            if (ch == '{') {
                ++state.inlineJsonDepth;
                continue;
            }
            if (ch != '}') {
                continue;
            }

            --state.inlineJsonDepth;
            if (state.inlineJsonDepth > 0) {
                continue;
            }

            if (!IsToolCallJsonText(state.inlineJsonBuffer)) {
                appendSpeakableText(state.inlineJsonBuffer);
            }
            state.inlineJsonBuffer.clear();
            state.inInlineJson = false;
            state.inlineJsonDepth = 0;
            state.inlineJsonInString = false;
            state.inlineJsonEscape = false;
            continue;
        }

        if (!state.inFence && ch == '{') {
            if (!state.fenceBuffer.empty()) {
                appendSpeakableText(state.fenceBuffer);
                state.fenceBuffer.clear();
            }
            state.inlineJsonBuffer.assign(1, ch);
            state.inInlineJson = true;
            state.inlineJsonDepth = 1;
            state.inlineJsonInString = false;
            state.inlineJsonEscape = false;
            continue;
        }

        state.fenceBuffer.push_back(ch);

        if (state.fenceBuffer.size() >= 3 &&
            state.fenceBuffer.compare(state.fenceBuffer.size() - 3, 3, "```") == 0) {
            if (state.fenceBuffer.size() > 3) {
                const std::string prefix = state.fenceBuffer.substr(0, state.fenceBuffer.size() - 3);
                if (!state.inFence) {
                    appendSpeakableText(prefix);
                }
            }

            state.fenceBuffer.clear();
            state.inFence = !state.inFence;
            continue;
        }

        if (state.fenceBuffer.size() < 3) {
            continue;
        }

        const char committed = state.fenceBuffer.front();
        state.fenceBuffer.erase(state.fenceBuffer.begin());
        if (!state.inFence) {
            appendSpeakableText(std::string(1, committed));
        }
    }
}

void GoogleAiInterpreter::EmitCompletedSpeech(
    std::string& pendingSpeech,
    bool flushAll,
    const InterpreterStreamCallback& onPartialResponse) const {
    if (!onPartialResponse) {
        if (flushAll) {
            pendingSpeech.clear();
        }
        return;
    }

    std::size_t emitUntil = std::string::npos;
    if (flushAll) {
        emitUntil = pendingSpeech.size();
    } else {
        const std::size_t lastDelimiter = pendingSpeech.find_last_of(".?!\n");
        if (lastDelimiter == std::string::npos) {
            return;
        }
        emitUntil = lastDelimiter + 1;
    }

    const std::string readyText = Trim(pendingSpeech.substr(0, emitUntil));
    pendingSpeech.erase(0, emitUntil);
    if (readyText.empty()) {
        return;
    }

    InterpreterResponse partialResponse;
    partialResponse.segments.push_back(ResponseSegment{ResponseSegmentType::Speech, readyText, true});
    onPartialResponse(partialResponse);
}

InterpreterResponse GoogleAiInterpreter::BuildStructuredResponse(const std::string& rawText) const {
    const std::string trimmed = Trim(rawText);
    InterpreterResponse response;
    if (trimmed.empty()) {
        return response;
    }

    if ((trimmed.front() == '{' && trimmed.back() == '}') || (trimmed.front() == '[' && trimmed.back() == ']')) {
        response.segments.push_back(ResponseSegment{ResponseSegmentType::Json, trimmed, false});
        return response;
    }

    std::size_t cursor = 0;
    while (cursor < rawText.size()) {
        const std::size_t fenceStart = rawText.find("```", cursor);
        if (fenceStart == std::string::npos) {
            AppendParsedTextSegments(response, rawText.substr(cursor));
            break;
        }

        AppendParsedTextSegments(response, rawText.substr(cursor, fenceStart - cursor));

        const std::size_t languageStart = fenceStart + 3;
        const std::size_t newlinePos = rawText.find('\n', languageStart);
        if (newlinePos == std::string::npos) {
            AppendIfNotEmpty(response, ResponseSegmentType::Code, rawText.substr(fenceStart + 3));
            break;
        }

        const std::string language = rawText.substr(languageStart, newlinePos - languageStart);
        const std::size_t fenceEnd = rawText.find("```", newlinePos + 1);
        if (fenceEnd == std::string::npos) {
            AppendIfNotEmpty(response, SegmentTypeFromFenceLanguage(language), rawText.substr(newlinePos + 1));
            break;
        }

        AppendIfNotEmpty(
            response,
            SegmentTypeFromFenceLanguage(language),
            rawText.substr(newlinePos + 1, fenceEnd - (newlinePos + 1))
        );
        cursor = fenceEnd + 3;
    }

    if (response.segments.empty()) {
        response.segments.push_back(ResponseSegment{ResponseSegmentType::Speech, trimmed, true});
    }

    return response;
}

}  // namespace voice_agent
