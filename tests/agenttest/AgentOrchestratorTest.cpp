#include "AgentOrchestratorTest.h"
#include <iostream>
#include <stdexcept>

namespace voice_agent {

InterpreterResponse MockInterpreter::Interpret(
    const InterpreterInput& input,
    const InterpreterStreamCallback& onPartialResponse,
    const CancellationToken* token) {
    lastInput = input;
    if (responses.empty()) return InterpreterResponse();
    auto res = responses.front();
    responses.erase(responses.begin());
    return res;
}

MockTool::MockTool(std::string name) : name_(name) {
    def_.name = name;
}

const ToolDefinition& MockTool::Definition() const {
    return def_;
}

ToolResult MockTool::Execute(const ToolCall& call, const CancellationToken* token) const {
    executedCount++;
    ToolResult res;
    res.succeeded = true;
    res.summary = name_ + " executed";
    return res;
}

TEST_F(AgentOrchestratorTest, TestMultipleToolCallsInOneTurn) {
    MockInterpreter interpreter;
    
    // Step 1: 2 tool calls in separate segments
    InterpreterResponse res;
    res.segments.push_back({ResponseSegmentType::Json, "{\"tool\": \"tool1\", \"arguments\": {}}"});
    res.segments.push_back({ResponseSegmentType::Json, "{\"tool\": \"tool2\", \"arguments\": {}}"});
    interpreter.responses.push_back(res);
    
    // Step 2: Final response
    InterpreterResponse finalRes;
    finalRes.segments.push_back({ResponseSegmentType::Speech, "Done"});
    interpreter.responses.push_back(finalRes);

    MockTool t1("tool1");
    MockTool t2("tool2");
    std::vector<ITool*> tools = {&t1, &t2};
    
    AppConfig config;
    config.maxAgentSteps = 5;
    
    AgentOrchestrator orchestrator(interpreter, tools, config);
    
    auto result = orchestrator.RunTurn("hello", nullptr);
    
    EXPECT_EQ(t1.executedCount, 1);
    EXPECT_EQ(t2.executedCount, 1);
    EXPECT_EQ(result.executedCalls.size(), 2);
    
    EXPECT_NE(interpreter.lastInput.text.find("Arac sonuclari"), std::string::npos);
    EXPECT_NE(interpreter.lastInput.text.find("tool1"), std::string::npos);
    EXPECT_NE(interpreter.lastInput.text.find("tool2"), std::string::npos);
}

TEST_F(AgentOrchestratorTest, TestJsonArrayToolCalls) {
    MockInterpreter interpreter;
    
    // Step 1: 2 tool calls in ONE segment as an array
    InterpreterResponse res;
    res.segments.push_back({ResponseSegmentType::Json, "[{\"tool\": \"tool1\"}, {\"tool\": \"tool2\"}]"});
    interpreter.responses.push_back(res);
    
    // Step 2: Final response
    InterpreterResponse finalRes;
    finalRes.segments.push_back({ResponseSegmentType::Speech, "Done"});
    interpreter.responses.push_back(finalRes);

    MockTool t1("tool1");
    MockTool t2("tool2");
    std::vector<ITool*> tools = {&t1, &t2};
    
    AppConfig config;
    config.maxAgentSteps = 5;
    
    AgentOrchestrator orchestrator(interpreter, tools, config);
    
    orchestrator.RunTurn("hello", nullptr);
    
    EXPECT_EQ(t1.executedCount, 1);
    EXPECT_EQ(t2.executedCount, 1);
}

} // namespace voice_agent
