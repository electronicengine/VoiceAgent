#pragma once

#include <string>
#include <vector>

namespace voice_agent {

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
};

}  // namespace voice_agent
