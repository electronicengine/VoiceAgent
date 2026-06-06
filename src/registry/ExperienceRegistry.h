#pragma once

#include "registry/BaseRegistry.h"

namespace voice_agent {

struct Experience {
    int id;
    std::string actionText;
    std::string resultText;
};

class ExperienceRegistry : public BaseRegistry {
public:
    ExperienceRegistry(SqliteDatabase& db, LlamaOperator& llama);
    
    bool Initialize() override;
    // Loads experiences from directory and syncs with DB
    void LoadFromDirectory(const std::string& directory);
    void AddExperience(const std::string& actionText, const std::string& resultText);
    void DeleteExperience(int id);
    std::vector<Experience> MatchExperiences(const std::string& actionText, float threshold = 0.7f, int limit = 3);
};

} // namespace voice_agent
