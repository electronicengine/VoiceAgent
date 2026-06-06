#pragma once

#include "registry/BaseRegistry.h"

namespace voice_agent {

struct Note {
    int id;
    std::string text;
};

class NoteRegistry : public BaseRegistry {
public:
    NoteRegistry(SqliteDatabase& db, LlamaOperator& llama);
    
    bool Initialize() override;
    
    // Loads notes from directory and syncs with DB
    void LoadFromDirectory(const std::string& directory);

    void AddNote(const std::string& text);
    void DeleteNote(int id);
    std::vector<Note> MatchNotes(const std::string& userText, float threshold = 0.7f, int limit = 3);
};

} // namespace voice_agent
