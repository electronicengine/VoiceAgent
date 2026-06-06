#include "SqliteDatabaseTest.h"
#include "registry/SqliteDatabase.h"
#include "RegistryTestHelper.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(SqliteDatabaseTest, TestOpenAndExecute) {
    std::string dbPath = "test_sqlite.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    EXPECT_TRUE(db.Open());
    EXPECT_TRUE(db.Execute("CREATE TABLE test (id INTEGER, val TEXT);"));
    EXPECT_TRUE(db.Execute("INSERT INTO test VALUES (1, 'hello');"));
    db.Close();
    CleanupTestDb(dbPath);
}

TEST_F(SqliteDatabaseTest, TestQuery) {
    std::string dbPath = "test_sqlite_query.db";
    CleanupTestDb(dbPath);
    SqliteDatabase db(dbPath);
    EXPECT_TRUE(db.Open());
    db.Execute("CREATE TABLE test (id INTEGER, val TEXT);");
    db.Execute("INSERT INTO test VALUES (1, 'hello');");
    
    bool found = false;
    db.Query("SELECT val FROM test WHERE id=1", [&](int argc, char** argv, char**){
        if (argc > 0 && argv[0] && std::string(argv[0]) == "hello") found = true;
        return 0;
    });
    EXPECT_TRUE(found);
    db.Close();
    CleanupTestDb(dbPath);
}

} // namespace voice_agent
