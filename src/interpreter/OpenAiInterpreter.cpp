#include "interpreter/OpenAiInterpreter.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

OpenAiInterpreter::OpenAiInterpreter(const AppConfig& config)
    : config_(config) {}

void OpenAiInterpreter::ResetSession(std::string systemPrompt) {
    if (config_.openAiAssistantId.empty()) {
        assistantId_ = CreateAssistant(systemPrompt);
    } else {
        assistantId_ = config_.openAiAssistantId;
    }
    threadId_ = CreateThread();
}

InterpreterResponse OpenAiInterpreter::Interpret(
    const std::string& userText,
    const InterpreterStreamCallback& onPartialResponse) {
    if (assistantId_.empty() || threadId_.empty()) {
        throw std::runtime_error("OpenAI session is not initialized.");
    }

    AddUserMessageToThread(userText);
    StreamParseState state;
    CreateRun(state, onPartialResponse);
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

    const InterpreterResponse finalResponse = BuildStructuredResponse(state.rawResponse);
    if (finalResponse.Empty()) {
        throw std::runtime_error("OpenAI returned an empty assistant response.");
    }

    return finalResponse;
}

std::vector<std::string> OpenAiInterpreter::DefaultHeaders() const {
    return {
        "Content-Type: application/json",
        "Authorization: Bearer " + config_.openAiApiKey,
        "OpenAI-Beta: assistants=v2",
    };
}

std::string OpenAiInterpreter::CreateAssistant(const std::string& instructions) const {
    const json requestBody = {
        {"model", config_.openAiModel},
        {"instructions", instructions},
        {"temperature", 0.4},
    };

    HttpRequest request;
    request.url = config_.openAiBaseUrl + "/assistants";
    request.headers = DefaultHeaders();
    request.body = requestBody.dump();

    const HttpResponse response = httpClient_.Post(request);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "OpenAI assistant creation failed with HTTP " +
            std::to_string(response.statusCode) + ": " + response.body
        );
    }

    const json responseJson = json::parse(response.body);
    return responseJson.at("id").get<std::string>();
}

std::string OpenAiInterpreter::CreateThread() const {
    HttpRequest request;
    request.url = config_.openAiBaseUrl + "/threads";
    request.headers = DefaultHeaders();
    request.body = json::object().dump();

    const HttpResponse response = httpClient_.Post(request);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "OpenAI thread creation failed with HTTP " +
            std::to_string(response.statusCode) + ": " + response.body
        );
    }

    const json responseJson = json::parse(response.body);
    return responseJson.at("id").get<std::string>();
}

void OpenAiInterpreter::AddUserMessageToThread(const std::string& userText) const {
    const json requestBody = {
        {"role", "user"},
        {"content", userText},
    };

    HttpRequest request;
    request.url = config_.openAiBaseUrl + "/threads/" + threadId_ + "/messages";
    request.headers = DefaultHeaders();
    request.body = requestBody.dump();

    const HttpResponse response = httpClient_.Post(request);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "OpenAI message creation failed with HTTP " +
            std::to_string(response.statusCode) + ": " + response.body
        );
    }
}

void OpenAiInterpreter::CreateRun(
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    const json requestBody = {
        {"assistant_id", assistantId_},
        {"stream", true},
    };

    HttpRequest request;
    request.url = config_.openAiBaseUrl + "/threads/" + threadId_ + "/runs";
    request.headers = DefaultHeaders();
    request.body = requestBody.dump();

    const HttpResponse response = httpClient_.PostStream(
        request,
        [this, &state, &onPartialResponse](const std::string& chunk) {
            ConsumeStreamingChunk(chunk, state, onPartialResponse);
        }
    );
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw std::runtime_error(
            "OpenAI run creation failed with HTTP " +
            std::to_string(response.statusCode) + ": " + response.body
        );
    }
}

void OpenAiInterpreter::ConsumeStreamingChunk(
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

        std::stringstream eventStream(rawEvent);
        std::string line;
        std::string eventName;
        std::string payload;
        while (std::getline(eventStream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.size() >= 7 && line.compare(0, 7, "event: ") == 0) {
                eventName = line.substr(7);
                continue;
            }
            if (line.size() >= 6 && line.compare(0, 6, "data: ") == 0) {
                if (!payload.empty()) {
                    payload += "\n";
                }
                payload += line.substr(6);
            }
        }

        ConsumeSseEvent(eventName, payload, state, onPartialResponse);
    }
}

void OpenAiInterpreter::ConsumeSseEvent(
    const std::string& eventName,
    const std::string& payload,
    StreamParseState& state,
    const InterpreterStreamCallback& onPartialResponse) const {
    const std::string label = eventName.empty() ? "message" : eventName;
    // if (!payload.empty()) {
    //     std::clog << " payload=" << payload;
    // }
    // std::clog << '\n';

    if (payload.empty() || payload == "[DONE]" || label == "done") {
        return;
    }

    const json eventJson = json::parse(payload, nullptr, false);
    if (eventJson.is_discarded()) {
        return;
    }

    const std::string deltaText = ExtractTextDelta(eventJson, label);
    if (!deltaText.empty()) {
        ConsumeStreamDelta(deltaText, state, onPartialResponse);
        return;
    }

    if (label == "thread.message.completed" && state.rawResponse.empty()) {
        const std::string text = ExtractMessageText(eventJson);
        if (!text.empty()) {
            ConsumeStreamDelta(text, state, onPartialResponse);
        }
    }
}

void OpenAiInterpreter::ConsumeStreamDelta(
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

std::string OpenAiInterpreter::ExtractTextDelta(const json& eventJson, const std::string& eventName) const {
    if (eventName == "response.output_text.delta" && eventJson.contains("delta") && eventJson.at("delta").is_string()) {
        return eventJson.at("delta").get<std::string>();
    }

    if (eventJson.contains("delta") && eventJson.at("delta").is_object()) {
        const auto& delta = eventJson.at("delta");

        if (delta.contains("content") && delta.at("content").is_array()) {
            std::ostringstream combined;
            for (const auto& item : delta.at("content")) {
                if (!item.is_object()) {
                    continue;
                }

                if (item.contains("text") && item.at("text").is_object()) {
                    const auto& text = item.at("text");
                    if (text.contains("value") && text.at("value").is_string()) {
                        combined << text.at("value").get<std::string>();
                        continue;
                    }
                }
                if (item.contains("text") && item.at("text").is_string()) {
                    combined << item.at("text").get<std::string>();
                    continue;
                }
                if (item.contains("value") && item.at("value").is_string()) {
                    combined << item.at("value").get<std::string>();
                }
            }
            return combined.str();
        }

        if (delta.contains("text") && delta.at("text").is_object()) {
            const auto& text = delta.at("text");
            if (text.contains("value") && text.at("value").is_string()) {
                return text.at("value").get<std::string>();
            }
        }
        if (delta.contains("text") && delta.at("text").is_string()) {
            return delta.at("text").get<std::string>();
        }
    }

    return "";
}

std::string OpenAiInterpreter::ExtractMessageText(const json& messageJson) const {
    if (!messageJson.contains("content") || !messageJson.at("content").is_array()) {
        return "";
    }

    std::ostringstream combined;
    for (const auto& block : messageJson.at("content")) {
        if (!block.is_object() || !block.contains("type")) {
            continue;
        }
        if (block.at("type") != "text") {
            continue;
        }
        if (!block.contains("text") || !block.at("text").is_object()) {
            continue;
        }
        const auto& text = block.at("text");
        if (!text.contains("value") || !text.at("value").is_string()) {
            continue;
        }
        combined << text.at("value").get<std::string>();
    }

    return combined.str();
}

InterpreterResponse OpenAiInterpreter::BuildStructuredResponse(const std::string& rawText) const {
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

void OpenAiInterpreter::ConsumeResponseText(
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

void OpenAiInterpreter::EmitCompletedSpeech(
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

}  // namespace voice_agent