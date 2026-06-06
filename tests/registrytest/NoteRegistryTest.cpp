#include "NoteRegistryTest.h"
#include "registry/NoteRegistry.h"
#include "registry/SqliteDatabase.h"
#include "RegistryTestHelper.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(NoteRegistryTest, TestAddAndMatchNote) {
    std::string dbPath = "test_note.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    NoteRegistry noteReg(db, mockLlama);
    noteReg.Initialize();

    noteReg.AddNote("sample note");
    
    auto matches = noteReg.MatchNotes("sample note");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches[0].text, "sample note");
    
    db.Close();
    CleanupTestDb(dbPath);
}

TEST_F(NoteRegistryTest, TestDeleteNote) {
    std::string dbPath = "test_note_del.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    db.Open();
    MockLlamaOperator mockLlama;
    NoteRegistry noteReg(db, mockLlama);
    noteReg.Initialize();

    noteReg.AddNote("to be deleted");
    auto matches = noteReg.MatchNotes("to be deleted");
    ASSERT_FALSE(matches.empty());

    int id = matches[0].id;
    noteReg.DeleteNote(id);
    matches = noteReg.MatchNotes("to be deleted");
    EXPECT_TRUE(matches.empty());

    db.Close();
    CleanupTestDb(dbPath);
}

} // namespace voice_agent
