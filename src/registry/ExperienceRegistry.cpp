#include "registry/ExperienceRegistry.h"
#include "common/logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace voice_agent {

ExperienceRegistry::ExperienceRegistry(SqliteDatabase& db, LlamaOperator& llama) 
    : BaseRegistry(db, llama) {}

bool ExperienceRegistry::Initialize() {
    std::string sql = "CREATE TABLE IF NOT EXISTS experiences ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "experience_embedding BLOB, "
                      "experience_action_text TEXT UNIQUE, "
                      "experience_result_text TEXT);";
    return db_.Execute(sql);
}

void ExperienceRegistry::LoadFromDirectory(const std::string& directory) {
    if (directory.empty() || !std::filesystem::exists(directory)) return;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") {
            try {
                std::ifstream file(entry.path());
                nlohmann::json j;
                file >> j;
                std::string actionText = j.at("action_text");
                std::string result = j.value("result", "");
                AddExperience(actionText, result);
            } catch (const std::exception& e) {
                ERROR("Failed to load experience file {}: {}", entry.path().string(), e.what());
            }
        }
    }
}

void ExperienceRegistry::AddExperience(const std::string& actionText, const std::string& resultText) {
    if (actionText.empty()) return;

    // Check if it already exists
    sqlite3_stmt* check_stmt;
    const char* check_sql = "SELECT id FROM experiences WHERE experience_action_text = ?;";
    if (sqlite3_prepare_v2(db_.GetHandle(), check_sql, -1, &check_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, actionText.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(check_stmt) == SQLITE_ROW);
        sqlite3_finalize(check_stmt);
        if (exists) return; // Already exists
    }

    std::vector<float> embedding = llama_.calculateEmbeddings(actionText);
    if (embedding.empty()) return;

    std::vector<char> blob = SerializeEmbedding(embedding);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO experiences (experience_embedding, experience_action_text, experience_result_text) VALUES (?, ?, ?);";
    
    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, blob.data(), blob.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, actionText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, resultText.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            ERROR("Failed to insert experience: {}", sqlite3_errmsg(db_.GetHandle()));
        }
        sqlite3_finalize(stmt);
    }
}

void ExperienceRegistry::DeleteExperience(int id) {
    std::string sql = "DELETE FROM experiences WHERE id = " + std::to_string(id) + ";";
    db_.Execute(sql);
}

std::vector<Experience> ExperienceRegistry::MatchExperiences(const std::string& actionText, float threshold, int limit) {
    std::vector<float> userEmb = llama_.calculateEmbeddings(actionText);
    if (userEmb.empty()) return {};

    struct ScoredExp {
        float score;
        int id;
        std::string actionText;
        std::string resultText;
    };
    std::vector<ScoredExp> scored;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, experience_embedding, experience_action_text, experience_result_text FROM experiences;";

    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const void* blob = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            const char* textPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* resTextPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            
            std::string actionTxt = textPtr ? textPtr : "";
            std::string resultTxt = resTextPtr ? resTextPtr : "";

            std::vector<float> expEmb = DeserializeEmbedding(blob, size);
            float sim = llama_.getSimilarity(userEmb, expEmb);

            if (sim >= threshold) {
                scored.push_back({sim, id, actionTxt, resultTxt});
            }
        }
        sqlite3_finalize(stmt);
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });

    std::vector<Experience> results;
    for (int i = 0; i < std::min((int)scored.size(), limit); ++i) {
        results.push_back({scored[i].id, scored[i].actionText, scored[i].resultText});
    }
    return results;
}

} // namespace voice_agent
