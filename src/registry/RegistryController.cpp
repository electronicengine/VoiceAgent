#include "registry/RegistryController.h"
#include <sstream>

namespace voice_agent {

RegistryController::RegistryController(const std::string& dbPath, LlamaOperator& llama)
    : db_(dbPath), llama_(llama) {
    skills_ = std::make_unique<SkillRegistry>(db_, llama_);
    experiences_ = std::make_unique<ExperienceRegistry>(db_, llama_);
    notes_ = std::make_unique<NoteRegistry>(db_, llama_);
}

RegistryController::~RegistryController() = default;

bool RegistryController::Initialize() {
    if (!db_.Open()) return false;
    
    bool ok = skills_->Initialize() && 
              experiences_->Initialize() && 
              notes_->Initialize();

    if (ok) {
        // Automatically sync with default local directories if they exist
        SyncWithFileSystem();
    }
    return ok;
}

void RegistryController::SyncWithFileSystem(const std::string& skillsDir, 
                                            const std::string& experiencesDir, 
                                            const std::string& notesDir) {
    LoadSkills(skillsDir);
    LoadExperiences(experiencesDir);
    LoadNotes(notesDir);
}

void RegistryController::LoadSkills(const std::string& directory) {
    skills_->LoadFromDirectory(directory);
}

void RegistryController::LoadExperiences(const std::string& directory) {
    experiences_->LoadFromDirectory(directory);
}

void RegistryController::LoadNotes(const std::string& directory) {
    notes_->LoadFromDirectory(directory);
}

std::string RegistryController::GetEnhancedPrompt(const std::string& userText) {
    std::ostringstream oss;

    // 1. Skill Matching
    auto matchedSkills = skills_->MatchSkills(userText);
    for (const auto& skill : matchedSkills) {
        oss << "--- [SKILL: " << skill.name << "] body ---\n"
            << skill.body << "\n--- [/SKILL] ---\n\n";
    }

    // 2. Experience Matching
    auto matchedExps = experiences_->MatchExperiences(userText);
    if (!matchedExps.empty()) {
        oss << "--- [RELEVANT EXPERIENCES] ---\n";
        for (const auto& exp : matchedExps) {
            oss << "- Action: " << exp.actionText << "\n";
            if (!exp.resultText.empty()) {
                oss << "  Result: " << exp.resultText << "\n";
            }
        }
        oss << "--- [/RELEVANT EXPERIENCES] ---\n\n";
    }

    // 3. Note Matching
    auto matchedNotes = notes_->MatchNotes(userText);
    if (!matchedNotes.empty()) {
        oss << "--- [RELEVANT NOTES] ---\n";
        for (const auto& note : matchedNotes) {
            oss << "- " << note.text << "\n";
        }
        oss << "--- [/RELEVANT NOTES] ---\n\n";
    }

    return oss.str();
}

} // namespace voice_agent
