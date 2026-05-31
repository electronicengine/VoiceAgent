#include "skills/SkillRegistry.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace voice_agent {

namespace {

constexpr const char* kFrontmatterDelim = "---";

// Splits a skill markdown file into (frontmatterJson, body). Front matter
// must be the first non-empty content of the file, delimited by lines that
// contain only "---". The frontmatter content itself is parsed as JSON.
bool SplitFrontmatter(const std::string& content, std::string& frontmatter, std::string& body) {
    std::istringstream stream(content);
    std::string line;
    // Skip leading blank lines.
    while (std::getline(stream, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed != kFrontmatterDelim) {
            // No frontmatter; treat entire content as body.
            body = content;
            return false;
        }
        break;
    }

    std::ostringstream fmBuffer;
    bool closed = false;
    while (std::getline(stream, line)) {
        if (Trim(line) == kFrontmatterDelim) {
            closed = true;
            break;
        }
        fmBuffer << line << '\n';
    }

    if (!closed) {
        body = content;
        return false;
    }

    frontmatter = fmBuffer.str();

    std::ostringstream bodyBuffer;
    bodyBuffer << stream.rdbuf();
    body = Trim(bodyBuffer.str());
    return true;
}

bool ParseSkillFile(const std::filesystem::path& path, Skill& skill) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[SkillRegistry] cannot open " << path.string() << "\n";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    std::string frontmatter;
    std::string body;
    if (!SplitFrontmatter(content, frontmatter, body)) {
        std::cerr << "[SkillRegistry] missing JSON frontmatter in "
                  << path.string() << "; skipped\n";
        return false;
    }

    nlohmann::json meta;
    try {
        meta = nlohmann::json::parse(frontmatter, nullptr, true, true);
    } catch (const nlohmann::json::exception& ex) {
        std::cerr << "[SkillRegistry] frontmatter parse error in "
                  << path.string() << ": " << ex.what() << "\n";
        return false;
    }

    if (!meta.is_object()) {
        std::cerr << "[SkillRegistry] frontmatter must be a JSON object in "
                  << path.string() << "\n";
        return false;
    }

    skill.filePath = path.string();
    skill.body = body;
    skill.name = meta.value("name", path.stem().string());
    skill.description = meta.value("description", std::string{});
    skill.triggerMode = ToLower(meta.value("triggerMode", std::string{"any"}));
    skill.priority = meta.value("priority", 0);
    skill.alwaysOn = meta.value("alwaysOn", false);

    skill.triggers.clear();
    const auto triggersIt = meta.find("triggers");
    if (triggersIt != meta.end() && triggersIt->is_array()) {
        for (const auto& entry : *triggersIt) {
            if (!entry.is_string()) {
                continue;
            }
            std::string lower = ToLower(entry.get<std::string>());
            const std::string trimmed = Trim(lower);
            if (!trimmed.empty()) {
                skill.triggers.push_back(trimmed);
            }
        }
    }

    if (skill.name.empty()) {
        std::cerr << "[SkillRegistry] skill name is empty in "
                  << path.string() << "; skipped\n";
        return false;
    }

    return true;
}

}  // namespace

void SkillRegistry::Load(const std::string& directory) {
    skills_.clear();
    if (directory.empty()) {
        return;
    }
    const std::filesystem::path dir(directory);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".md") {
            continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        Skill skill;
        if (ParseSkillFile(path, skill)) {
            skills_.push_back(std::move(skill));
        }
    }
}

std::string SkillRegistry::RenderIndex() const {
    if (skills_.empty()) {
        return {};
    }
    std::ostringstream out;
    for (const auto& skill : skills_) {
        out << "- " << skill.name;
        if (!skill.description.empty()) {
            out << " - " << skill.description;
        }
        out << "\n";
    }
    return out.str();
}

std::vector<const Skill*> SkillRegistry::MatchSkills(
    const std::string& userText,
    std::size_t maxSkills
) const {
    std::vector<const Skill*> matched;
    if (skills_.empty() || maxSkills == 0) {
        return matched;
    }

    const std::string lowerText = ToLower(userText);

    for (const auto& skill : skills_) {
        if (skill.alwaysOn) {
            matched.push_back(&skill);
            continue;
        }
        if (skill.triggers.empty()) {
            continue;
        }
        bool isMatch = false;
        if (skill.triggerMode == "all") {
            isMatch = true;
            for (const auto& trigger : skill.triggers) {
                if (lowerText.find(trigger) == std::string::npos) {
                    isMatch = false;
                    break;
                }
            }
        } else {
            for (const auto& trigger : skill.triggers) {
                if (lowerText.find(trigger) != std::string::npos) {
                    isMatch = true;
                    break;
                }
            }
        }
        if (isMatch) {
            matched.push_back(&skill);
        }
    }

    std::stable_sort(
        matched.begin(),
        matched.end(),
        [](const Skill* a, const Skill* b) { return a->priority > b->priority; }
    );

    if (matched.size() > maxSkills) {
        matched.resize(maxSkills);
    }
    return matched;
}

std::string SkillRegistry::RenderInjection(const std::vector<const Skill*>& selected) {
    if (selected.empty()) {
        return {};
    }
    std::ostringstream out;
    for (const auto* skill : selected) {
        out << "[SKILL: " << skill->name << "]\n";
        out << skill->body << "\n";
        out << "[/SKILL]\n\n";
    }
    return out.str();
}

}  // namespace voice_agent
