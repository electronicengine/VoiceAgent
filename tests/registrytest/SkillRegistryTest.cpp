#include "SkillRegistryTest.h"
#include "registry/SkillRegistry.h"
#include "registry/SqliteDatabase.h"
#include "RegistryTestHelper.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace voice_agent {

TEST_F(SkillRegistryTest, TestAddAndMatchSkill) {
    std::string dbPath = "test_skill.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    SkillRegistry skillReg(db, mockLlama);
    skillReg.Initialize();

    std::string skillFile = "test_skill.md";
    std::ofstream ofs(skillFile);
    ofs << "---\n{\n  \"name\": \"test_skill\",\n  \"description\": \"test description\"\n}\n---\ntest body";
    ofs.close();

    skillReg.AddSkill(skillFile);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT description FROM skills WHERE file_path = ?;";
    bool descriptionStored = false;
    if (sqlite3_prepare_v2(db.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, skillFile.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* description = sqlite3_column_text(stmt, 0);
            descriptionStored = description != nullptr && std::string(reinterpret_cast<const char*>(description)) == "test description";
        }
        sqlite3_finalize(stmt);
    }
    EXPECT_TRUE(descriptionStored);
    
    auto matches = skillReg.MatchSkills("test description");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches[0].name, "test_skill");
    EXPECT_EQ(matches[0].body, "test body");
    
    std::filesystem::remove(skillFile);
    db.Close();
    CleanupTestDb(dbPath);
}

TEST_F(SkillRegistryTest, TestRemoveSkill) {
    std::string dbPath = "test_skill_remove.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    SkillRegistry skillReg(db, mockLlama);
    skillReg.Initialize();

    std::string skillFile = "test_skill_rem.md";
    std::ofstream ofs(skillFile);
    ofs << "---\n{\n  \"name\": \"test_skill\",\n  \"description\": \"test description\"\n}\n---\ntest body";
    ofs.close();

    skillReg.AddSkill(skillFile);
    skillReg.RemoveSkill(skillFile);

    auto matches = skillReg.MatchSkills("test description");
    EXPECT_TRUE(matches.empty());

    std::filesystem::remove(skillFile);
    db.Close();
    CleanupTestDb(dbPath);
}

TEST_F(SkillRegistryTest, TestSyncFromDirectory) {
    std::string dbPath = "test_skill_sync.db";
    CleanupTestDb(dbPath);
    std::filesystem::path testDir = "test_skill_sync_dir";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directory(testDir);

    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    SkillRegistry skillReg(db, mockLlama);
    skillReg.Initialize();

    std::filesystem::path activeSkillFile = testDir / "active_skill.md";
    std::filesystem::path staleSkillFile = testDir / "stale_skill.md";

    {
        std::ofstream ofs(activeSkillFile);
        ofs << "---\n{\n  \"name\": \"active_skill\",\n  \"description\": \"active desc\"\n}\n---\nactive body";
    }
    {
        std::ofstream ofs(staleSkillFile);
        ofs << "---\n{\n  \"name\": \"stale_skill\",\n  \"description\": \"stale desc old\"\n}\n---\nstale body";
    }

    skillReg.AddSkill(activeSkillFile.string());
    skillReg.AddSkill(staleSkillFile.string());

    {
        std::ofstream ofs(activeSkillFile);
        ofs << "---\n{\n  \"name\": \"active_skill\",\n  \"description\": \"active desc updated\"\n}\n---\nactive body updated";
    }
    std::filesystem::remove(staleSkillFile);

    skillReg.LoadFromDirectory(testDir.string());

    auto updatedMatches = skillReg.MatchSkills("active desc updated");
    ASSERT_FALSE(updatedMatches.empty());
    EXPECT_EQ(updatedMatches[0].name, "active_skill");

    auto staleMatches = skillReg.MatchSkills("stale desc old");
    for (const auto& m : staleMatches) {
        EXPECT_NE(m.name, "stale_skill");
    }

    db.Close();
    std::filesystem::remove_all(testDir);
    CleanupTestDb(dbPath);
}

} // namespace voice_agent
