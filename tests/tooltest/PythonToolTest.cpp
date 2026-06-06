#include "PythonToolTest.h"
#include "tools/PythonTool.h"
#include "tools/ProjectFilesTool.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(PythonToolTest, TestIntegrationWithProjectFiles) {
    ProjectFilesTool pft(scriptsRoot);
    const std::string scriptName = "py_int_test.py";
    
    ToolCall write; write.name="ProjectFilesTool";
    write.arguments = {{"path",scriptName},{"content","import json\nprint(json.dumps({'status':'ok'}))"}};
    pft.Execute(write, nullptr);

    AppConfig config; config.resolvedPythonToolScriptRoot = scriptsRoot.string();
    PythonTool pt(config, nullptr);
    ToolCall run; run.name="PythonTool"; run.arguments={{"script",scriptName}};
    auto res = pt.Execute(run, nullptr);
    
    EXPECT_TRUE(res.succeeded);
    EXPECT_EQ(res.output.at("output").value("status",""), "ok");
}

TEST_F(PythonToolTest, TestComprehensiveParameters) {
    ProjectFilesTool pft(scriptsRoot);
    const std::string sn = "params.py";
    ToolCall w; w.name="ProjectFilesTool";
    w.arguments = {{"path",sn},{"content","import sys, os, json\nprint(json.dumps({'e':os.environ.get('K',''),'a':sys.argv[1:]}))"}};
    pft.Execute(w, nullptr);

    AppConfig config; config.resolvedPythonToolScriptRoot = scriptsRoot.string();
    PythonTool pt(config, nullptr);
    ToolCall run; run.name="PythonTool";
    run.arguments = {{"script",sn},{"env", {{"K","V"}}},{"args",{"a1"}}};
    auto res = pt.Execute(run, nullptr);
    
    auto out = res.output.at("output");
    EXPECT_EQ(out.value("e",""), "V");
    EXPECT_EQ(out.at("a").size(), 1);
}


TEST_F(PythonToolTest, TestWebRunnerNavigation) {
    AppConfig config;
    config.resolvedPythonToolScriptRoot = scriptsRoot.string();
    PythonTool pt(config, nullptr);

    nlohmann::json webConfig = {
        {"artifactsDir", (binaryRoot / "artifacts" / "test_webrunner").string()},
        {"headless", false},
        {"steps", nlohmann::json::array({
            {
                {"action", "goto"},
                {"url", "https://www.linkedin.com/notifications/"}
            },
            {
                {"action", "wait_for_timeout"},
                {"timeoutMs", 3000}
            }
        })}
    };

    ToolCall run;
    run.name = "PythonTool";
    run.arguments = {
        {"script", "wb_runner.py"}, 
        {"packages", nlohmann::json::array({"playwright"})}, 
        {"args", nlohmann::json::array({"--inline-config", webConfig.dump()})}
    };

    auto res = pt.Execute(run, nullptr);

    EXPECT_TRUE(res.succeeded);
    
    auto scriptOutput = res.output.at("output");
    EXPECT_TRUE(scriptOutput.is_object());
    EXPECT_TRUE(scriptOutput.value("ok", false));
    EXPECT_FALSE(scriptOutput.value("title", "").empty());
}

} // namespace voice_agent
