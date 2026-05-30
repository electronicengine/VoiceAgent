#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace voice_agent {

struct AccountRecord {
    std::string id;
    std::string displayName;
    std::string provider;        // "google" | "github" | "generic" | ...
    std::string loginUrl;
    std::string loggedInUrl;
    std::string loginCheckSelector;
    std::string email;           // sensitive; never returned to LLM
    std::string password;        // sensitive; never returned to LLM
    std::filesystem::path profileDir;  // resolved absolute path
};

// Loads accounts from a JSON file (e.g. account.json next to voiceAgentConfig.json).
// Holds credentials in-process; Lookup is the only way to access them.
class AccountStore {
public:
    AccountStore();

    // Load accounts from `accountsFile`. Per-account profile dirs are resolved
    // relative to `defaultRoot` when the account does not provide an absolute
    // profileDir. Missing file is not fatal: the store stays empty.
    // Throws on malformed JSON.
    void Load(const std::filesystem::path& accountsFile,
              const std::filesystem::path& defaultRoot);

    bool Loaded() const { return loaded_; }
    bool Empty() const { return accounts_.empty(); }
    std::vector<std::string> AccountIds() const;

    std::optional<AccountRecord> Find(const std::string& id) const;

    // Public listing for prompts/system-prompt: never includes credentials.
    struct PublicEntry {
        std::string id;
        std::string displayName;
        std::string provider;
        std::string loginUrl;
    };
    std::vector<PublicEntry> PublicListing() const;

    const std::filesystem::path& AccountsRootDir() const { return rootDir_; }

private:
    std::filesystem::path rootDir_;
    std::map<std::string, AccountRecord> accounts_;
    bool loaded_ = false;
};

}  // namespace voice_agent
