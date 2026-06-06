#include "registry/NoteRegistry.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace voice_agent {

NoteRegistry::NoteRegistry(SqliteDatabase& db, LlamaOperator& llama) 
    : BaseRegistry(db, llama) {}

bool NoteRegistry::Initialize() {
    std::string sql = "CREATE TABLE IF NOT EXISTS notes ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "note_embedding BLOB, "
                      "note_text TEXT UNIQUE);";
    return db_.Execute(sql);
}

void NoteRegistry::LoadFromDirectory(const std::string& directory) {
    if (directory.empty() || !std::filesystem::exists(directory)) return;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".json") {
            try {
                std::ifstream file(entry.path());
                nlohmann::json j;
                file >> j;
                std::string text = j.at("text");
                AddNote(text);
            } catch (const std::exception& e) {
                std::cerr << "Failed to load note file " << entry.path() << ": " << e.what() << std::endl;
            }
        }
    }
}

void NoteRegistry::AddNote(const std::string& text) {
    if (text.empty()) return;

    // Check if it already exists
    sqlite3_stmt* check_stmt;
    const char* check_sql = "SELECT id FROM notes WHERE note_text = ?;";
    if (sqlite3_prepare_v2(db_.GetHandle(), check_sql, -1, &check_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(check_stmt) == SQLITE_ROW);
        sqlite3_finalize(check_stmt);
        if (exists) return; // Already exists
    }

    std::vector<float> embedding = llama_.calculateEmbeddings(text);
    if (embedding.empty()) return;

    std::vector<char> blob = SerializeEmbedding(embedding);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO notes (note_embedding, note_text) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, blob.data(), blob.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to insert note: " << sqlite3_errmsg(db_.GetHandle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

void NoteRegistry::DeleteNote(int id) {
    std::string sql = "DELETE FROM notes WHERE id = " + std::to_string(id) + ";";
    db_.Execute(sql);
}

std::vector<Note> NoteRegistry::MatchNotes(const std::string& userText, float threshold, int limit) {
    std::vector<float> userEmb = llama_.calculateEmbeddings(userText);
    if (userEmb.empty()) return {};

    struct ScoredNote {
        float score;
        int id;
        std::string text;
    };
    std::vector<ScoredNote> scored;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, note_embedding, note_text FROM notes;";

    if (sqlite3_prepare_v2(db_.GetHandle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const void* blob = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            std::string text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            std::vector<float> noteEmb = DeserializeEmbedding(blob, size);
            float sim = llama_.getSimilarity(userEmb, noteEmb);

            if (sim >= threshold) {
                scored.push_back({sim, id, text});
            }
        }
        sqlite3_finalize(stmt);
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });

    std::vector<Note> results;
    for (int i = 0; i < std::min((int)scored.size(), limit); ++i) {
        results.push_back({scored[i].id, scored[i].text});
    }
    return results;
}

} // namespace voice_agent
