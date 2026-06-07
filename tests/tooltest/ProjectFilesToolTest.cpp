#include "ProjectFilesToolTest.h"
#include "tools/ProjectFilesTool.h"
#include "agent/AgentOrchestrator.h"
#include <gtest/gtest.h>
#include <fstream>

namespace voice_agent {

TEST_F(ProjectFilesToolTest, TestScriptCreationThroughOrchestrator) {
    const std::filesystem::path scriptFile = scriptsRoot / "demo_script.py";
    std::error_code ec; std::filesystem::remove(scriptFile, ec);
    ProjectFilesTool pft(scriptsRoot);

    SequenceInterpreter interpreter;
    interpreter.responses.push_back(MakeJsonToolCallResponse(
        R"pft({"tool":"ProjectFilesTool","arguments":{"path":"demo_script.py","content":"print(1)"}})pft"));
    interpreter.responses.push_back(MakeSpeechResponse("tamam"));

    AppConfig config; config.maxAgentSteps = 2;
    AgentOrchestrator orchestrator(interpreter, {&pft}, config, nullptr);
    orchestrator.RunTurn("script yaz", {}, nullptr, {});

    EXPECT_TRUE(std::filesystem::exists(scriptFile));
}

TEST_F(ProjectFilesToolTest, TestUsesBinaryDirRoots) {
    const std::filesystem::path outsideRoot = std::filesystem::temp_directory_path() / "va_outside";
    ProjectFilesTool pft(outsideRoot);
    
    ToolCall call;
    call.name = "ProjectFilesTool";
    call.arguments = {{"path", "root_probe.py"}, {"content", "test"}};
    pft.Execute(call, nullptr);
    
    EXPECT_TRUE(std::filesystem::exists(scriptsRoot / "root_probe.py"));
}

TEST_F(ProjectFilesToolTest, TestRejectsDirectorySelection) {
    ProjectFilesTool pft(scriptsRoot);
    ToolCall call;
    call.name = "ProjectFilesTool";
    call.arguments = {{"path", "nested/bad.md"}, {"content", "..."}};
    auto res = pft.Execute(call, nullptr);
    EXPECT_FALSE(res.succeeded);
    EXPECT_TRUE(res.blockedByPolicy);
}

TEST_F(ProjectFilesToolTest, TestScriptReadAndReplace) {
    const std::string name = "read_replace_test.py";
    ProjectFilesTool pft(scriptsRoot);
    
    ToolCall write; write.name="ProjectFilesTool";
    write.arguments = {{"path",name},{"content","old"}};
    pft.Execute(write, nullptr);

    ToolCall replace; replace.name="ProjectFilesTool";
    replace.arguments = {{"operation","replace"},{"path",name},{"findText","old"},{"replaceWith","new"}};
    pft.Execute(replace, nullptr);

    std::ifstream f(scriptsRoot / name);
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(c.find("new"), std::string::npos);
}

TEST_F(ProjectFilesToolTest, TestCanReadSpecificLineRange) {
    const std::string name = "lines.py";
    ProjectFilesTool pft(scriptsRoot);
    ToolCall w; w.name="ProjectFilesTool";
    w.arguments = {{"path",name},{"content","l1\nl2\nl3\nl4\n"}};
    pft.Execute(w, nullptr);

    ToolCall r; r.name="ProjectFilesTool";
    r.arguments = {{"operation","read"},{"path",name},{"startLine",2},{"endLine",3}};
    auto res = pft.Execute(r, nullptr);
    EXPECT_EQ(res.output.value("content", ""), "l2\nl3\n");
}

TEST_F(ProjectFilesToolTest, TestCanReplaceAndInsertSpecificSection) {
    const std::string name = "section.py";
    ProjectFilesTool pft(scriptsRoot);
    ToolCall w; w.name="ProjectFilesTool";
    w.arguments = {{"path",name},{"content","h\nm\nf\n"}};
    pft.Execute(w, nullptr);

    ToolCall ins; ins.name="ProjectFilesTool";
    ins.arguments = {{"operation","insert"},{"path",name},{"insertLine",2},{"content","in\n"}};
    pft.Execute(ins, nullptr);

    std::ifstream f(scriptsRoot / name);
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(c, "h\nin\nm\nf\n");
}

} // namespace voice_agent
