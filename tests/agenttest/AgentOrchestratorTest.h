#pragma once

#include "agent/AgentOrchestrator.h"
#include "interpreter/IInterpreter.h"
#include "tools/ITool.h"
#include "config/AppConfig.h"
#include <gtest/gtest.h>
#include <vector>
#include <string>

namespace voice_agent {

class MockInterpreter : public IInterpreter {
public:
    void ResetSession(std::string systemPrompt) override {}
    
    InterpreterResponse Interpret(
        const InterpreterInput& input,
        const InterpreterStreamCallback& onPartialResponse,
        const CancellationToken* token = nullptr) override;

    std::vector<InterpreterResponse> responses;
    InterpreterInput lastInput;
};

class MockTool : public ITool {
public:
    MockTool(std::string name);
    const ToolDefinition& Definition() const override;
    ToolResult Execute(const ToolCall& call, const CancellationToken* token = nullptr) const override;

    std::string name_;
    ToolDefinition def_;
    mutable int executedCount = 0;
};

class AgentOrchestratorTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

} // namespace voice_agent


