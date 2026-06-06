#include "RegistryToolTest.h"
#include "tools/RegistryTool.h"
#include "registry/RegistryController.h"
#include <gtest/gtest.h>
#include <fstream>

namespace voice_agent {

class RegistryToolTestFixture : public RegistryToolTest {
protected:
    void SetUp() override {
        ToolTest::SetUp();
        std::filesystem::remove("test_registry.db");
        std::filesystem::remove(skillsRoot / "test_skill.md");
    }

    void TearDown() override {
        std::filesystem::remove("test_registry.db");
        std::filesystem::remove(skillsRoot / "test_skill.md");
        ToolTest::TearDown();
    }
};

TEST_F(RegistryToolTestFixture, TestRecordNote) {
    LlamaOperator llama;
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", LLAMA_POOLING_TYPE_CLS);
    RegistryController rc("test_registry.db", llama);
    rc.Initialize();
    
    RegistryTool tool(rc, binaryRoot);
    ToolCall call;
    call.name = "RegistryTool";
    call.arguments = {{"operation", "record_note"}, {"text", "Test note content"}};
    
    auto res = tool.Execute(call, nullptr);
    EXPECT_TRUE(res.succeeded);
    
    auto notes = rc.GetNoteRegistry().MatchNotes("Test note", 0.1f);
    EXPECT_FALSE(notes.empty());
}

TEST_F(RegistryToolTestFixture, TestRecordExperience) {
    LlamaOperator llama;
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", LLAMA_POOLING_TYPE_CLS);

    RegistryController rc("test_registry.db", llama);
    rc.Initialize();
    
    RegistryTool tool(rc, binaryRoot);
    ToolCall call;
    call.name = "RegistryTool";
    call.arguments = {{"operation", "record_experience"}, {"action_text", "Tried ls"}, {"result", "Listed files"}};
    
    auto res = tool.Execute(call, nullptr);
    EXPECT_TRUE(res.succeeded);
    
    auto exps = rc.GetExperienceRegistry().MatchExperiences("Tried ls", 0.1f);
    EXPECT_FALSE(exps.empty());
}

TEST_F(RegistryToolTestFixture, TestRecordSkill) {
    LlamaOperator llama;
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", LLAMA_POOLING_TYPE_CLS);

    RegistryController rc("test_registry.db", llama);
    rc.Initialize();
    
    RegistryTool tool(rc, binaryRoot);
    ToolCall call;
    call.name = "RegistryTool";
    call.arguments = {
        {"operation", "record_skill"},
        {"name", "test_skill"},
        {"description", "A test skill"},
        {"body", "echo hello"}
    };
    
    auto res = tool.Execute(call, nullptr);
    EXPECT_TRUE(res.succeeded) << res.summary;
    EXPECT_TRUE(std::filesystem::exists(skillsRoot / "test_skill.md"));
    
    auto skills = rc.GetSkillRegistry().MatchSkills("test skill", 0.1f);
    EXPECT_FALSE(skills.empty());
    if (!skills.empty()) {
        EXPECT_EQ(skills[0].body, "echo hello");
    }
}

TEST_F(RegistryToolTestFixture, TestQuery) {
    LlamaOperator llama;
    llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", LLAMA_POOLING_TYPE_CLS);

    RegistryController rc("test_registry.db", llama);
    rc.Initialize();
    
    rc.GetNoteRegistry().AddNote("Meeting at 2pm");
    
    RegistryTool tool(rc, binaryRoot);
    ToolCall call;
    call.name = "RegistryTool";
    call.arguments = {{"operation", "query"}, {"query", "meeting"}};
    
    auto res = tool.Execute(call, nullptr);
    EXPECT_TRUE(res.succeeded);
    EXPECT_TRUE(res.output.contains("notes"));
    EXPECT_FALSE(res.output["notes"].empty());
}

} // namespace voice_agent
