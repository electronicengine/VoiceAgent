#include "ShellToolTest.h"
#include "tools/ShellTool.h"
#include "tools/ProjectFilesTool.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(ShellToolTest, TestCommandExecution) {
    ShellTool st(scriptsRoot);
    ToolCall call;
    call.name = "ShellTool";
    call.arguments = {{"command", "echo 'Hello'"}};
    
    auto res = st.Execute(call, nullptr);
    EXPECT_TRUE(res.succeeded);
    EXPECT_NE(res.summary.find("Hello"), std::string::npos);
}

TEST_F(ShellToolTest, TestIntegrationWithProjectFiles) {
    ProjectFilesTool pft(scriptsRoot);
    const std::string name = "test_shell.sh";
    std::error_code ec;
    std::filesystem::remove(scriptsRoot / name, ec);

    ToolCall w;
    w.name = "ProjectFilesTool";
    w.arguments = {{"path",name},{"content","#!/bin/bash\necho \"args: $1 $2\""}};
    pft.Execute(w, nullptr);

    ShellTool st(scriptsRoot);
    ToolCall run;
    run.name = "ShellTool";
    run.arguments = {{"script", name}, {"args", {"A", "B"}}};
    
    auto res = st.Execute(run, nullptr);
    EXPECT_TRUE(res.succeeded);
    EXPECT_NE(res.summary.find("args: A B"), std::string::npos);
    
    std::filesystem::remove(scriptsRoot / name, ec);
}

} // namespace voice_agent
