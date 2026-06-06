#include "ExperienceRegistryTest.h"
#include "registry/ExperienceRegistry.h"
#include "registry/SqliteDatabase.h"
#include "RegistryTestHelper.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(ExperienceRegistryTest, TestAddAndMatchExperience) {
    std::string dbPath = "test_exp.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    ExperienceRegistry expReg(db, mockLlama);
    expReg.Initialize();

    expReg.AddExperience("action text", "result text");
    
    auto matches = expReg.MatchExperiences("action text");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches[0].actionText, "action text");
    EXPECT_EQ(matches[0].resultText, "result text");
    
    db.Close();
    CleanupTestDb(dbPath);
}

TEST_F(ExperienceRegistryTest, TestDeleteExperience) {
    std::string dbPath = "test_exp_del.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    ExperienceRegistry expReg(db, mockLlama);
    expReg.Initialize();

    expReg.AddExperience("to be deleted", "result");
    auto matches = expReg.MatchExperiences("to be deleted");
    ASSERT_FALSE(matches.empty());

    int id = matches[0].id;
    expReg.DeleteExperience(id);
    matches = expReg.MatchExperiences("to be deleted");
    EXPECT_TRUE(matches.empty());

    db.Close();
    CleanupTestDb(dbPath);
}

} // namespace voice_agent
