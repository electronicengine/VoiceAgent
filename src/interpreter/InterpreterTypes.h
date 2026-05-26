#pragma once

#include "common/StringUtils.h"

#include <functional>
#include <string>
#include <vector>

namespace voice_agent {

struct ConversationMessage {
    std::string role;
    std::string content;
};

using InterpreterStreamCallback = std::function<void(const struct InterpreterResponse&)>;

enum class ResponseSegmentType {
    Speech,
    Json,
    Code,
    Command,
    Metadata,
};

struct ResponseSegment {
    ResponseSegmentType type = ResponseSegmentType::Speech;
    std::string content;
    bool speakable = true;
};

struct InterpreterResponse {
    std::vector<ResponseSegment> segments;

    bool Empty() const {
        for (const auto& segment : segments) {
            if (!Trim(segment.content).empty()) {
                return false;
            }
        }
        return true;
    }

    std::string DisplayText() const {
        std::string output;
        for (const auto& segment : segments) {
            const std::string content = Trim(segment.content);
            if (content.empty()) {
                continue;
            }
            if (!output.empty()) {
                output += "\n\n";
            }
            output += content;
        }
        return Trim(output);
    }

    std::string SpeakableText() const {
        std::string output;
        for (const auto& segment : segments) {
            if (!segment.speakable) {
                continue;
            }
            const std::string content = Trim(segment.content);
            if (content.empty()) {
                continue;
            }
            if (!output.empty()) {
                output += " ";
            }
            output += content;
        }
        return Trim(output);
    }
};

}  // namespace voice_agent