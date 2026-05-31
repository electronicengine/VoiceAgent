#pragma once

#include <optional>
#include <string>
#include <vector>

namespace voice_agent {

struct SkillAccountConfig {
    std::string id;
    std::string loginUrl;
    std::string loggedInUrl;
    std::string loginCheckSelector;
};

struct Skill {
    std::string name;
    std::string description;
    std::string filePath;
    std::string body;
    // Lower-cased substring triggers. Entries wrapped as "/regex/" are not
    // currently supported; we keep substring matching simple and predictable.
    std::vector<std::string> triggers;
    // "any" (default): match if any trigger appears. "all": all must appear.
    std::string triggerMode = "any";
    // Higher priority wins when too many skills match in a single turn.
    int priority = 0;
    // alwaysOn=true skills are injected on every turn (use sparingly).
    bool alwaysOn = false;
    // Optional account metadata used by WebBrowserTool/login bootstrap flows.
    std::optional<SkillAccountConfig> account;
};

}  // namespace voice_agent
