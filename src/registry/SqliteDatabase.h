#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace voice_agent {

class SqliteDatabase {
public:
    SqliteDatabase(const std::string& dbPath);
    ~SqliteDatabase();

    bool Open();
    void Close();

    bool Execute(const std::string& sql);
    
    // Callback function for SELECT queries
    using QueryCallback = std::function<int(int, char**, char**)>;
    bool Query(const std::string& sql, QueryCallback callback);

    sqlite3* GetHandle() const { return db_; }

private:
    std::string dbPath_;
    sqlite3* db_ = nullptr;
};

} // namespace voice_agent
