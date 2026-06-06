#pragma once

#include "registry/BaseRegistry.h"
#include <optional>

namespace voice_agent {

struct Skill {
    int id;
    std::string name;
    std::string description;
    std::string filePath;
    std::string body;
};

class SkillRegistry : public BaseRegistry {
public:
    SkillRegistry(SqliteDatabase& db, LlamaOperator& llama);
    
    bool Initialize() override;
    
    // Loads skills from directory and syncs with DB
    void LoadFromDirectory(const std::string& directory);

    // Returns matched skill bodies
    std::vector<Skill> MatchSkills(const std::string& userText, float threshold = 0.4f, int limit = 2);

    void AddSkill(const std::string& filePath);
    void RemoveSkill(const std::string& filePath);

private:
    bool ParseSkillFile(const std::string& path, Skill& skill);
};

} // namespace voice_agent
