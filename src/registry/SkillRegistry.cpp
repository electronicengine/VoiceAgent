#include "registry/SkillRegistry.h"
#include "common/StringUtils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <unordered_set>

namespace voice_agent {

namespace {

bool HasSkillsDescriptionColumn(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "PRAGMA table_info(skills);";
    bool found = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* columnName = sqlite3_column_text(stmt, 1);
            if (columnName != nullptr && std::string(reinterpret_cast<const char*>(columnName)) == "description") {
                found = true;
                break;
            }
        }
        sqlite3_finalize(stmt);
    }

    return found;
}

} // namespace

SkillRegistry::SkillRegistry(SqliteDatabase& db, LlamaOperator& llama) 
    : BaseRegistry(db, llama) {}

bool SkillRegistry::Initialize() {
    std::string sql = "CREATE TABLE IF NOT EXISTS skills ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "embedding BLOB, "
                      "description TEXT, "
                      "file_path TEXT UNIQUE);";

    if (!db_.Execute(sql)) {
        return false;
    }

    if (!HasSkillsDescriptionColumn(db_.GetHandle())) {
        return db_.Execute("ALTER TABLE skills ADD COLUMN description TEXT;");
    }

    return true;
}

void SkillRegistry::LoadFromDirectory(const std::string& directory) {
    if (directory.empty() || !std::filesystem::exists(directory)) return;

    std::unordered_set<std::string> existingPaths;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT file_path FROM skills;";

        if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* filePath = sqlite3_column_text(stmt, 0);
                if (filePath != nullptr) {
                    existingPaths.emplace(reinterpret_cast<const char*>(filePath));
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    std::unordered_set<std::string> seenPaths;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") {
            continue;
        }

        std::string filePath = entry.path().string();
        seenPaths.emplace(filePath);
        AddSkill(filePath);
    }

    for (const auto& filePath : existingPaths) {
        if (seenPaths.find(filePath) == seenPaths.end()) {
            RemoveSkill(filePath);
        }
    }
}

void SkillRegistry::AddSkill(const std::string& filePath) {
    Skill skill;
    if (!ParseSkillFile(filePath, skill)) return;

    std::cout << "Adding skill: " << skill.name << " from file: " << filePath << "\n";
    std::vector<float> embedding = llama_.calculateEmbeddings(skill.description);
    if (embedding.empty()) return;

    std::vector<char> blob = SerializeEmbedding(embedding);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO skills (embedding, description, file_path) VALUES (?, ?, ?);";
    
    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, blob.data(), blob.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, skill.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, filePath.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to insert skill: " << sqlite3_errmsg(db_.GetHandle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

void SkillRegistry::RemoveSkill(const std::string& filePath) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM skills WHERE file_path = ?;";

    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to delete skill: " << sqlite3_errmsg(db_.GetHandle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

std::vector<Skill> SkillRegistry::MatchSkills(const std::string& userText, float threshold, int limit) {
    std::vector<float> userEmb = llama_.calculateEmbeddings(userText);
    if (userEmb.empty()) return {};

    std::vector<Skill> results;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, embedding, file_path FROM skills;";

    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        struct ScoredSkill {
            float score;
            std::string filePath;
        };
        std::vector<ScoredSkill> scored;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            std::string filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            std::vector<float> skillEmb = DeserializeEmbedding(blob, size);
            float sim = llama_.getSimilarity(userEmb, skillEmb);

            if (sim >= threshold) {
                scored.push_back({sim, filePath});
            }
        }
        sqlite3_finalize(stmt);

        for(const auto& s : scored) {
            std::cout << "Matched skill file: " << s.filePath << " with similarity: " << s.score << "\n";
        }

        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.score > b.score;
        });

        for (int i = 0; i < std::min((int)scored.size(), limit); ++i) {
            Skill skill;
            if (ParseSkillFile(scored[i].filePath, skill)) {
                results.push_back(skill);
            }
        }
    }
    return results;
}

bool SkillRegistry::ParseSkillFile(const std::string& path, Skill& skill) {
    std::ifstream file(path);
    if (!file) return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    size_t open_pos = content.find("---");
    if (open_pos == std::string::npos) {
        return false;
    }

    size_t fm_start = open_pos + 3;
    size_t close_pos = content.find("---", fm_start);
    if (close_pos == std::string::npos) {
        return false;
    }

    std::string json_part = content.substr(fm_start, close_pos - fm_start);
    std::string body_part = content.substr(close_pos + 3);

    json_part = Trim(json_part);
    body_part = Trim(body_part);

    // Sanitize trailing commas in JSON (e.g. from linkedin_account.md)
    try {
        std::regex trailing_comma_regex(",(\\s*[}\\]])");
        json_part = std::regex_replace(json_part, trailing_comma_regex, "$1");
    } catch (...) {
        // Fallback
    }

    try {
        auto j = nlohmann::json::parse(json_part);
        skill.name = j.value("name", "");
        skill.description = j.value("description", "");
        skill.body = body_part;
        skill.filePath = path;
        return !skill.name.empty();
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse skill file JSON part: " << path << ", error: " << e.what() << std::endl;
        return false;
    }
}

} // namespace voice_agent
