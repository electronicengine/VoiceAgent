#include "registry/SqliteDatabase.h"
#include <iostream>

namespace voice_agent {

SqliteDatabase::SqliteDatabase(const std::string& dbPath) : dbPath_(dbPath) {}

SqliteDatabase::~SqliteDatabase() {
    Close();
}

bool SqliteDatabase::Open() {
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return true;
}

void SqliteDatabase::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDatabase::Execute(const std::string& sql) {
    char* zErrMsg = 0;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool SqliteDatabase::Query(const std::string& sql, QueryCallback callback) {
    char* zErrMsg = 0;
    auto wrapped_callback = [](void* data, int argc, char** argv, char** azColName) -> int {
        auto& cb = *static_cast<QueryCallback*>(data);
        return cb(argc, argv, azColName);
    };

    int rc = sqlite3_exec(db_, sql.c_str(), wrapped_callback, &callback, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

} // namespace voice_agent
