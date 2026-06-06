#include "RegistryControllerTest.h"
#include "registry/RegistryController.h"
#include "RegistryTestHelper.h"
#include <gtest/gtest.h>

namespace voice_agent {

    TEST_F(RegistryControllerTest, TestInitialization) {
        std::string dbPath = "test_controller_init.db";
        CleanupTestDb(dbPath);
        MockLlamaOperator mockLlama;
        RegistryController controller(dbPath, mockLlama);
        EXPECT_TRUE(controller.Initialize());
        CleanupTestDb(dbPath);
    }

    TEST_F(RegistryControllerTest, TestEnhancedPrompt) {
        std::string dbPath = "test_controller_prompt.db";
        CleanupTestDb(dbPath);
        MockLlamaOperator mockLlama;
        RegistryController controller(dbPath, mockLlama);
        controller.Initialize();

        controller.GetExperienceRegistry().AddExperience("working", "success");
        std::string prompt = controller.GetEnhancedPrompt("working");
        
        EXPECT_NE(prompt.find("RELEVANT EXPERIENCES"), std::string::npos);
        EXPECT_NE(prompt.find("Action: working"), std::string::npos);
        EXPECT_NE(prompt.find("Result: success"), std::string::npos);
        
        CleanupTestDb(dbPath);
    }

} // namespace voice_agent
