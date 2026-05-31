#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace voice_agent {

inline constexpr const char* kBrowserProfileModePersistentDir = "persistent_dir";
inline constexpr const char* kBrowserProfileModeSystemChrome = "system_chrome";

struct AccountRecord {
    std::string id;
    std::string displayName;
    std::string loginUrl;
    std::string loggedInUrl;
    std::string loginCheckSelector;
    std::string browserProfileMode = kBrowserProfileModePersistentDir;
    std::filesystem::path profileDir;  // resolved absolute path
    std::filesystem::path chromeUserDataDir;
    std::string chromeProfileDir;
    std::string chromeProfileName;
    bool requireChromeClosed = false;
};

struct BrowserProfileRecord {
    std::string id;
    std::string mode = kBrowserProfileModePersistentDir;
    std::filesystem::path profileDir;
    std::filesystem::path chromeUserDataDir;
    std::string chromeProfileDir;
    std::string chromeProfileName;
    bool requireChromeClosed = false;
};

// Loads accounts from a JSON file (e.g. account.json next to voiceAgentConfig.json).
// Holds credentials in-process; Lookup is the only way to access them.
class AccountStore {
public:
    AccountStore();

    // Load accounts from `accountsFile`. Accounts use the shared browser
    // profile referenced by top-level defaultSessionBrowserProfileId when it is
    // configured; otherwise they fall back to per-account persistent dirs under
    // `defaultRoot`. Missing file is not fatal: the store stays empty.
    // Throws on malformed JSON.
    void Load(const std::filesystem::path& accountsFile,
              const std::filesystem::path& defaultRoot);

    bool Loaded() const { return loaded_; }
    bool Empty() const { return accounts_.empty(); }
    std::vector<std::string> AccountIds() const;

    std::optional<AccountRecord> Find(const std::string& id) const;
    std::optional<BrowserProfileRecord> FindBrowserProfile(const std::string& id) const;
    std::optional<BrowserProfileRecord> DefaultSessionBrowserProfile() const;

    // Public listing for prompts/system-prompt: never includes credentials.
    struct PublicEntry {
        std::string id;
        std::string displayName;
        std::string loginUrl;
    };
    std::vector<PublicEntry> PublicListing() const;

    const std::filesystem::path& AccountsRootDir() const { return rootDir_; }

private:
    std::filesystem::path rootDir_;
    std::map<std::string, AccountRecord> accounts_;
    std::map<std::string, BrowserProfileRecord> browserProfiles_;
    std::string defaultSessionBrowserProfileId_;
    bool loaded_ = false;
};

}  // namespace voice_agent
