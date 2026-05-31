#pragma once

#include "skills/Skill.h"

#include <string>
#include <vector>

namespace voice_agent {

class SkillRegistry {
public:
    // Loads every "*.md" file under `directory` (non-recursive). If the
    // directory does not exist or is empty, the registry is initialized empty
    // (no error). Files that fail to parse are skipped with a warning to
    // stderr.
    void Load(const std::string& directory);

    const std::vector<Skill>& Skills() const { return skills_; }
    bool Empty() const { return skills_.empty(); }

    // Returns "name - description" lines for every loaded skill. Used as a
    // compact index inside the system prompt.
    std::string RenderIndex() const;

    // Selects skills whose triggers match `userText`. `alwaysOn` skills are
    // always included. The result is sorted by descending priority and capped
    // at `maxSkills`.
    std::vector<const Skill*> MatchSkills(
        const std::string& userText,
        std::size_t maxSkills
    ) const;

    // Returns account metadata declared in a skill frontmatter, if any.
    const SkillAccountConfig* FindAccountConfig(const std::string& accountId) const;

    // Concatenates the bodies of `selected` into a single block ready to be
    // prepended to the user message. Returns empty string when `selected` is
    // empty.
    static std::string RenderInjection(const std::vector<const Skill*>& selected);

private:
    std::vector<Skill> skills_;
};

}  // namespace voice_agent
