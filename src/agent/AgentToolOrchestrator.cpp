#include "agent/AgentToolOrchestrator.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace voice_agent {

namespace {

bool MatchesToolName(const ToolDefinition& definition, const std::string& toolName) {
    if (toolName == definition.name) {
        return true;
    }

    return std::find(definition.aliases.begin(), definition.aliases.end(), toolName) != definition.aliases.end();
}

}  // namespace

AgentToolOrchestrator::AgentToolOrchestrator(
        IInterpreter& interpreter,
        const std::vector<ITool*>& tools,
        const AppConfig& config)
    : interpreter_(interpreter),
            tools_(tools),
      maxAgentSteps_(config.maxAgentSteps) {}

AgentTurnResult AgentToolOrchestrator::RunTurn(
    const std::string& userText,
    const InterpreterStreamCallback& onPartialResponse,
    const CancellationToken* token) const {
    InterpreterInput currentInput;
    currentInput.text = userText;
    AgentTurnResult result;

    for (int step = 0; step < maxAgentSteps_; ++step) {
        if (token != nullptr && token->IsCancelled()) {
            return result;
        }
        std::cout << "Agent step " << (step + 1) << " of " << maxAgentSteps_ << "\n";
        const InterpreterResponse response = interpreter_.Interpret(currentInput, onPartialResponse, token);
        if (token != nullptr && token->IsCancelled()) {
            return result;
        }
        if (response.Empty()) {
            throw std::runtime_error("Interpreter returned an empty response.");
        }

        const ToolCall call = ParseToolCall(response);
        if (call.name.empty()) {
            std::cout << "No tool call detected in the response. Finalizing agent turn.\n\n";
            result.finalResponse = response;
            return result;
        }

        ToolResult toolResult;
        const ITool* tool = ResolveTool(call.name);

        std::cout << "Executing tool: " << call.name << " with arguments: " << call.arguments.dump() << "\n\n";
        if (tool == nullptr) {
            toolResult = ToolResult{
                false,
                false,
                "Istenen arac kayitli degil.",
                {{"reason", "unknown_tool"}, {"tool", call.name}}
            };
        } else {
            toolResult = tool->Execute(call, token);
        }
        std::cout << "Tool execution result: " << toolResult.summary << "\n\n";

        result.executedCalls.push_back(call);
        currentInput = FormatToolResultMessage(call, toolResult);
    }

    throw std::runtime_error("Agent step limit reached before producing a final response.");
}

ToolCall AgentToolOrchestrator::ParseToolCall(const InterpreterResponse& response) const {
    std::cout << response.DisplayText() << "\n\n";
    
    for (const auto& segment : response.segments) {
        if (segment.type != ResponseSegmentType::Json) {
            continue;
        }

        const nlohmann::json payload = nlohmann::json::parse(segment.content, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            continue;
        }
        if (!payload.contains("tool") || !payload.at("tool").is_string()) {
            continue;
        }

        ToolCall call;
        call.name = payload.at("tool").get<std::string>();
        if (payload.contains("arguments") && payload.at("arguments").is_object()) {
            call.arguments = payload.at("arguments");
        }

        std::cout << "Parsed tool call: " << call.name << " with arguments: " << call.arguments.dump() << "\n\n";
        return call;
    }

    return ToolCall{};
}

const ITool* AgentToolOrchestrator::ResolveTool(const std::string& toolName) const {
    for (const ITool* tool : tools_) {
        if (tool != nullptr && MatchesToolName(tool->Definition(), toolName)) {
            return tool;
        }
    }

    return nullptr;
}

InterpreterInput AgentToolOrchestrator::FormatToolResultMessage(const ToolCall& call, const ToolResult& result) const {
    InterpreterInput input;

    std::string message = "Arac sonucu\n";
    message += "Arac: " + call.name + "\n";
    message += std::string("Durum: ") + (result.succeeded ? "basarili" : "basarisiz") + "\n";
    if (result.blockedByPolicy) {
        message += "Politika: bu islem onay gerektirdigi icin engellendi.\n";
    }
    if (result.output.contains("exitCode") && result.output.at("exitCode").is_number_integer()) {
        message += "Exit code: " + std::to_string(result.output.at("exitCode").get<int>()) + "\n";
    }
    if (result.output.contains("reason") && result.output.at("reason").is_string()) {
        message += "Reason: " + result.output.at("reason").get<std::string>() + "\n";
    }
    if (!result.imageAttachments.empty()) {
        message += "Gorsel ekler: " + std::to_string(result.imageAttachments.size()) + "\n";
        message += "Not: Ekli ekran goruntulerini inceleyerek cevabini buna gore guncelle.\n";
    }
    message += "Ozet: " + result.summary;

    if (!result.output.empty()) {
        message += "\nCikti:\n" + result.output.dump(2);
    }

    message += "\nYonlendirme: Hedefe ulastiysan kullaniciya final cevabi ver. Hedefe ulasmadiysan ve adim hakkin kaldiysa yeni bir arac veya farkli bir yontem dene; kullanicidan sadece tekrar denemek icin yonlendirme isteme.";

    input.text = message;
    input.images.reserve(result.imageAttachments.size());
    for (const auto& attachment : result.imageAttachments) {
        input.images.push_back(InterpreterImageInput{attachment.filePath, attachment.detail});
    }

    return input;
}

}  // namespace voice_agent