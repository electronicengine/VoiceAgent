#pragma once

#include "registry/SkillRegistry.h"
#include "registry/ExperienceRegistry.h"
#include "registry/NoteRegistry.h"
#include "registry/SqliteDatabase.h"
#include "common/llama_operator.h"

namespace voice_agent {

class RegistryController {
public:
    RegistryController(const std::string& dbPath, LlamaOperator& llama);
    ~RegistryController();

    bool Initialize();
    
    // Syncs all registries with their respective directories
    void SyncWithFileSystem(const std::string& skillsDir = "skills", 
                            const std::string& experiencesDir = "experiences", 
                            const std::string& notesDir = "notes");

    // Loads skills from directory (call this during startup)
    void LoadSkills(const std::string& directory);
    void LoadExperiences(const std::string& directory);
    void LoadNotes(const std::string& directory);

    // Main entry point for matching all three types
    std::string GetEnhancedPrompt(const std::string& userText);

    SkillRegistry& GetSkillRegistry() { return *skills_; }
    ExperienceRegistry& GetExperienceRegistry() { return *experiences_; }
    NoteRegistry& GetNoteRegistry() { return *notes_; }

private:
    SqliteDatabase db_;
    LlamaOperator& llama_;
    
    std::unique_ptr<SkillRegistry> skills_;
    std::unique_ptr<ExperienceRegistry> experiences_;
    std::unique_ptr<NoteRegistry> notes_;
};

} // namespace voice_agent
