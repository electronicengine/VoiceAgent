#include "agent/AgentOrchestrator.h"

#include "common/StringUtils.h"
#include "common/logger.h"

#include <nlohmann/json.hpp>
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

bool ContainsAny(const std::string& haystack, const std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (needle != nullptr && haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

AgentOrchestrator::AgentOrchestrator(
        IInterpreter& interpreter,
        const std::vector<ITool*>& tools,
        const AppConfig& config,
        RegistryController* registryController)
    : interpreter_(interpreter),
            tools_(tools),
      registryController_(registryController),
      maxAgentSteps_(config.maxAgentSteps),
      maxSkillsPerTurn_(static_cast<std::size_t>(config.maxSkillsPerTurn > 0 ? config.maxSkillsPerTurn : 3)) {}

AgentTurnResult AgentOrchestrator::RunTurn(
    const std::string& userText,
    const InterpreterStreamCallback& onPartialResponse,
    const CancellationToken* token,
    const AnnouncementCallback& onAnnouncement) const {
    InterpreterInput currentInput;
    currentInput.text = userText;

    if (registryController_ != nullptr) {
        std::string enhancedPrompt = registryController_->GetEnhancedPrompt(userText, static_cast<int>(maxSkillsPerTurn_));
        if (!enhancedPrompt.empty()) {
            INFO("[Orchestrator] Enhanced prompt with registry matches:\n{}", enhancedPrompt);
            currentInput.text = enhancedPrompt + userText;
        }else
        {
            INFO("[Orchestrator] No relevant registry entries found for the user input.");
        }
    }

    AgentTurnResult result;
    int consecutiveEmptyResponses = 0;

    for (int step = 0; step < maxAgentSteps_; ++step) {
        if (token != nullptr && token->IsCancelled()) {
            return result;
        }
        INFO("Agent step {} of {}", (step + 1), maxAgentSteps_);
        const InterpreterResponse response = interpreter_.Interpret(currentInput, onPartialResponse, token);
        if (token != nullptr && token->IsCancelled()) {
            return result;
        }
        if (response.Empty()) {
            consecutiveEmptyResponses++;
            if (consecutiveEmptyResponses >= 2) {
                ERROR("Interpreter returned empty response multiple times. Aborting turn.");
                break;
            }
            WARNING("Interpreter returned an empty response. Asking the model to continue without ending the program.");
            currentInput.text =
                "Sistem notu: Son assistant cevabi bos geldi veya stream kayboldu. "
                "Mevcut baglami koruyarak ayni goreve devam et. "
                "Hedefe ulastiysan final cevabi ver. Ulasmadiysan gerekli arac cagrisini yeniden uret. "
                "Bos cevap verme.";
            currentInput.images.clear();
            continue;
        }
        consecutiveEmptyResponses = 0;

        const std::vector<ToolCall> calls = ParseToolCalls(response);
        if (calls.empty()) {
            INFO("No tool call detected in the response. Finalizing agent turn.");
            result.finalResponse = response;
            return result;
        }

        std::vector<std::pair<ToolCall, ToolResult>> stepResults;
        for (const auto& call : calls) {
            ToolResult toolResult;
            const ITool* tool = ResolveTool(call.name);

            INFO("Executing tool: {} with arguments: {}", call.name, call.arguments.dump());
            if (onAnnouncement) {
                onAnnouncement(call.name + " kullaniyorum.");
            }
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
            INFO("Tool execution result: {}", toolResult.summary);

            stepResults.push_back({call, toolResult});
            result.executedCalls.push_back(call);
        }

        currentInput = FormatToolResultMessage(
            stepResults,
            userText
        );
    }

    result.finalResponse = InterpreterResponse(); // Empty or failure
    result.finalResponse.segments.push_back({ResponseSegmentType::Speech, "Üzgünüm, bu görevi belirlenen adım sınırında tamamlamayı beceremedim."});
    return result;
}

std::vector<ToolCall> AgentOrchestrator::ParseToolCalls(const InterpreterResponse& response) const {
    std::vector<ToolCall> calls;
    
    for (const auto& segment : response.segments) {
        if (segment.type != ResponseSegmentType::Json) {
            continue;
        }

        const nlohmann::json payload = nlohmann::json::parse(segment.content, nullptr, false);
        if (payload.is_discarded()) {
            continue;
        }

        auto processPayload = [&](const nlohmann::json& p) {
            if (!p.is_object() || !p.contains("tool") || !p.at("tool").is_string()) {
                return;
            }

            ToolCall call;
            call.name = p.at("tool").get<std::string>();
            if (p.contains("arguments") && p.at("arguments").is_object()) {
                call.arguments = p.at("arguments");
            } else if (p.contains("parameters") && p.at("parameters").is_object()) {
                call.arguments = p.at("parameters");
            }
            calls.push_back(call);
            INFO("Parsed tool call: {} with arguments: {}", call.name, call.arguments.dump());
        };

        if (payload.is_array()) {
            for (const auto& item : payload) {
                processPayload(item);
            }
        } else {
            processPayload(payload);
        }
    }

    return calls;
}

const ITool* AgentOrchestrator::ResolveTool(const std::string& toolName) const {
    for (const ITool* tool : tools_) {
        if (tool != nullptr && MatchesToolName(tool->Definition(), toolName)) {
            return tool;
        }
    }

    return nullptr;
}

InterpreterInput AgentOrchestrator::FormatToolResultMessage(
    const std::vector<std::pair<ToolCall, ToolResult>>& stepResults,
    const std::string& /*originalUserText*/) const {
    InterpreterInput input;

    std::string message = "Arac sonuclari:\n";
    
    for (size_t i = 0; i < stepResults.size(); ++i) {
        const auto& call = stepResults[i].first;
        const auto& result = stepResults[i].second;

        message += "\n--- Arac " + std::to_string(i + 1) + " ---\n";
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
        message += "Ozet: " + result.summary + "\n";

        if (!result.output.empty()) {
            nlohmann::json finalOutput = result.output;
            bool isBrowserTool = false;
            
            // Handle nesting from PythonTool
            if (finalOutput.contains("output") && finalOutput.at("output").is_object()) {
                const auto& nested = finalOutput.at("output");
                if (nested.contains("results") && nested.at("results").is_array()) {
                    isBrowserTool = true;
                }
            } else if (finalOutput.contains("results") && finalOutput.at("results").is_array()) {
                isBrowserTool = true;
            }

            if (result.succeeded && isBrowserTool) {
                // wb_runner.py output is too detailed for success feedback. 
                // Only provide a slim version focusing on the final state and key fields.
                nlohmann::json slim = finalOutput;
                nlohmann::json& target = slim;
                if (slim.contains("output") && slim.at("output").is_object()) {
                    target = slim.at("output");
                }
                
                // IMPORTANT: Keep "output" field if it contains the snapshot text
                // But remove the huge "results" array which has per-step snapshots
                if (target.contains("results")) target.erase("results");
                if (target.contains("artifactsDir")) target.erase("artifactsDir");
                
                message += "Cikti (ozetlenmis):\n" + slim.dump(2) + "\n";
            } else {
                message += "Cikti:\n" + finalOutput.dump(2) + "\n";
            }
        }

        for (const auto& attachment : result.imageAttachments) {
            input.images.push_back(InterpreterImageInput{attachment.filePath, attachment.detail});
        }
    }

    if (!input.images.empty()) {
        message += "\nNot: Ekte " + std::to_string(input.images.size()) + " adet gorsel bulunmaktadir. Bunlari inceleyerek cevabini buna gore guncelle.\n";
    }

    DEBUG("[Orchestrator] Final tool output message sent to AI:\n{}", message);

    message += "\nYonlendirme: Hedefe ulastiysan kullaniciya final cevabi ver. Hedefe ulasmadiysan ve adim hakkin kaldiysa yeni bir arac veya farkli bir yontem dene; kullanicidan sadece tekrar denemek icin yonlendirme isteme.";
    message += " Orijinal istekte kalici bir script veya skill yazma talebi varsa, bunu ilgili tool ile tamamlamadan final verme.";

    input.text = message;
    return input;
}

}  // namespace voice_agent