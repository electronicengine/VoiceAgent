#include "config/AccountStore.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

namespace voice_agent {

namespace {

std::string HumanizeAccountId(const std::string& id) {
    std::string value = Trim(id);
    if (value.empty()) {
        return "hesap";
    }

    for (char& ch : value) {
        if (ch == '_' || ch == '-' || ch == '.') {
            ch = ' ';
        }
    }

    bool capitalizeNext = true;
    for (char& ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            capitalizeNext = true;
            continue;
        }
        ch = static_cast<char>(capitalizeNext ? std::toupper(static_cast<unsigned char>(ch))
                                              : std::tolower(static_cast<unsigned char>(ch)));
        capitalizeNext = false;
    }
    return value;
}

std::string ExpandHomePrefix(const std::string& pathText) {
    const std::string trimmed = Trim(pathText);
    if (trimmed.empty() || trimmed[0] != '~') {
        return trimmed;
    }

    const char* home = std::getenv("HOME");
    if (home == nullptr || std::string(home).empty()) {
        return trimmed;
    }
    if (trimmed.size() == 1) {
        return std::string(home);
    }
    if (trimmed[1] == '/') {
        return std::string(home) + trimmed.substr(1);
    }
    return trimmed;
}

std::filesystem::path ResolveRelative(
    const std::filesystem::path& base,
    const std::string& configured
) {
    const std::string trimmed = ExpandHomePrefix(configured);
    if (trimmed.empty()) {
        return {};
    }
    std::filesystem::path path(trimmed);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (base / path).lexically_normal();
}

std::string ReadStringField(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        throw std::runtime_error(std::string("Account field '") + key + "' must be a string.");
    }
    return Trim(it->get<std::string>());
}

bool ReadBoolField(const nlohmann::json& obj, const char* key, bool defaultValue) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return defaultValue;
    }
    if (!it->is_boolean()) {
        throw std::runtime_error(std::string("Account field '") + key + "' must be a boolean.");
    }
    return it->get<bool>();
}

BrowserProfileRecord BuildBrowserProfileRecord(
    const std::string& id,
    const nlohmann::json& browserProfile,
    const std::filesystem::path& configDir,
    const std::filesystem::path& rootDir
) {
    BrowserProfileRecord record;
    record.id = id;
    record.mode = ToLower(ReadStringField(browserProfile, "mode"));
    if (record.mode.empty()) {
        record.mode = kBrowserProfileModePersistentDir;
    }
    if (record.mode != kBrowserProfileModePersistentDir && record.mode != kBrowserProfileModeSystemChrome) {
        throw std::runtime_error(
            "Unsupported browser profile mode '" + record.mode + "' for browserProfileId '" + id + "'."
        );
    }

    if (record.mode == kBrowserProfileModeSystemChrome) {
        record.chromeUserDataDir = ResolveRelative(configDir, ReadStringField(browserProfile, "chromeUserDataDir"));
        record.chromeProfileDir = ReadStringField(browserProfile, "chromeProfileDir");
        record.chromeProfileName = ReadStringField(browserProfile, "chromeProfileName");
        record.requireChromeClosed = ReadBoolField(browserProfile, "requireChromeClosed", true);
    } else {
        const std::string sharedProfileDir = ReadStringField(browserProfile, "profileDir");
        if (!sharedProfileDir.empty()) {
            record.profileDir = ResolveRelative(configDir, sharedProfileDir);
        } else {
            record.profileDir = (rootDir / id).lexically_normal();
        }
    }
    return record;
}

const nlohmann::json* FindBrowserProfile(
    const nlohmann::json* browserProfilesJson,
    const std::string& profileId
) {
    if (browserProfilesJson == nullptr || profileId.empty()) {
        return nullptr;
    }
    const auto it = browserProfilesJson->find(profileId);
    if (it == browserProfilesJson->end()) {
        return nullptr;
    }
    if (!it->is_object()) {
        throw std::runtime_error("Browser profile must be an object (id='" + profileId + "').");
    }
    return &(*it);
}

}  // namespace

AccountStore::AccountStore() = default;

void AccountStore::Load(
    const std::filesystem::path& accountsFile,
    const std::filesystem::path& defaultRoot
) {
    accounts_.clear();
    browserProfiles_.clear();
    defaultSessionBrowserProfileId_.clear();
    rootDir_ = defaultRoot;
    loaded_ = false;

    if (accountsFile.empty() || !std::filesystem::exists(accountsFile)) {
        return;
    }

    std::ifstream file(accountsFile);
    if (!file) {
        throw std::runtime_error("Failed to open accounts file: " + accountsFile.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const nlohmann::json::exception& ex) {
        throw std::runtime_error(
            "Failed to parse accounts file '" + accountsFile.string() + "': " + ex.what()
        );
    }
    if (!json.is_object()) {
        throw std::runtime_error("Accounts file must contain a JSON object: " + accountsFile.string());
    }
    loaded_ = true;

    const std::filesystem::path configDir = accountsFile.parent_path();

    if (json.contains("accountsRootDir") && json.at("accountsRootDir").is_string()) {
        const std::string configured = Trim(json.at("accountsRootDir").get<std::string>());
        if (!configured.empty()) {
            rootDir_ = ResolveRelative(configDir, configured);
        }
    }
    if (rootDir_.empty()) {
        rootDir_ = defaultRoot;
    }

    const nlohmann::json* browserProfilesJson = nullptr;
    if (json.contains("browserProfiles")) {
        if (!json.at("browserProfiles").is_object()) {
            throw std::runtime_error("browserProfiles must be a JSON object.");
        }
        browserProfilesJson = &json.at("browserProfiles");
        for (auto it = browserProfilesJson->begin(); it != browserProfilesJson->end(); ++it) {
            if (!it.value().is_object()) {
                throw std::runtime_error("Browser profile must be an object (id='" + it.key() + "').");
            }
            const std::string profileId = Trim(it.key());
            if (profileId.empty()) {
                continue;
            }
            browserProfiles_.emplace(
                profileId,
                BuildBrowserProfileRecord(profileId, it.value(), configDir, rootDir_)
            );
        }
    }
    if (json.contains("defaultSessionBrowserProfileId") && json.at("defaultSessionBrowserProfileId").is_string()) {
        defaultSessionBrowserProfileId_ = Trim(json.at("defaultSessionBrowserProfileId").get<std::string>());
        if (!defaultSessionBrowserProfileId_.empty() && browserProfiles_.find(defaultSessionBrowserProfileId_) == browserProfiles_.end()) {
            throw std::runtime_error(
                "Unknown defaultSessionBrowserProfileId '" + defaultSessionBrowserProfileId_ + "'."
            );
        }
    }

    if (!json.contains("accounts") || !json.at("accounts").is_object()) {
        return;
    }

    const auto& accountsJson = json.at("accounts");
    for (auto it = accountsJson.begin(); it != accountsJson.end(); ++it) {
        if (!it.value().is_object()) {
            throw std::runtime_error("Each account must be an object (id='" + it.key() + "').");
        }
        const std::string id = Trim(it.key());
        if (id.empty()) {
            continue;
        }

        AccountRecord record;
        record.id = id;
        record.displayName = HumanizeAccountId(id);
        record.loginUrl = ReadStringField(it.value(), "loginUrl");
        record.loggedInUrl = ReadStringField(it.value(), "loggedInUrl");
        record.loginCheckSelector = ReadStringField(it.value(), "loginCheckSelector");

        if (!defaultSessionBrowserProfileId_.empty()) {
            const auto browserProfileIt = browserProfiles_.find(defaultSessionBrowserProfileId_);
            if (browserProfileIt == browserProfiles_.end()) {
                throw std::runtime_error(
                    "Unknown defaultSessionBrowserProfileId '" + defaultSessionBrowserProfileId_ +
                    "' while resolving account '" + id + "'."
                );
            }
            const BrowserProfileRecord& browserProfile = browserProfileIt->second;
            record.browserProfileMode = browserProfile.mode;
            record.profileDir = browserProfile.profileDir;
            record.chromeUserDataDir = browserProfile.chromeUserDataDir;
            record.chromeProfileDir = browserProfile.chromeProfileDir;
            record.chromeProfileName = browserProfile.chromeProfileName;
            record.requireChromeClosed = browserProfile.requireChromeClosed;
        } else {
            record.browserProfileMode = kBrowserProfileModePersistentDir;
            record.profileDir = (rootDir_ / id).lexically_normal();
        }

        accounts_.emplace(id, std::move(record));
    }
}

std::optional<BrowserProfileRecord> AccountStore::FindBrowserProfile(const std::string& id) const {
    const auto it = browserProfiles_.find(Trim(id));
    if (it == browserProfiles_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<BrowserProfileRecord> AccountStore::DefaultSessionBrowserProfile() const {
    if (defaultSessionBrowserProfileId_.empty()) {
        return std::nullopt;
    }
    return FindBrowserProfile(defaultSessionBrowserProfileId_);
}

std::vector<std::string> AccountStore::AccountIds() const {
    std::vector<std::string> ids;
    ids.reserve(accounts_.size());
    for (const auto& [id, _] : accounts_) {
        ids.push_back(id);
    }
    return ids;
}

std::optional<AccountRecord> AccountStore::Find(const std::string& id) const {
    const std::string trimmedId = Trim(id);
    const auto it = accounts_.find(trimmedId);
    if (it == accounts_.end()) {
        if (trimmedId.empty()) {
            return std::nullopt;
        }

        AccountRecord record;
        record.id = trimmedId;
        record.displayName = HumanizeAccountId(trimmedId);
        if (!defaultSessionBrowserProfileId_.empty()) {
            const auto browserProfileIt = browserProfiles_.find(defaultSessionBrowserProfileId_);
            if (browserProfileIt == browserProfiles_.end()) {
                return std::nullopt;
            }
            const BrowserProfileRecord& browserProfile = browserProfileIt->second;
            record.browserProfileMode = browserProfile.mode;
            record.profileDir = browserProfile.profileDir;
            record.chromeUserDataDir = browserProfile.chromeUserDataDir;
            record.chromeProfileDir = browserProfile.chromeProfileDir;
            record.chromeProfileName = browserProfile.chromeProfileName;
            record.requireChromeClosed = browserProfile.requireChromeClosed;
        } else {
            record.browserProfileMode = kBrowserProfileModePersistentDir;
            record.profileDir = (rootDir_ / trimmedId).lexically_normal();
        }
        return record;
    }
    return it->second;
}

std::vector<AccountStore::PublicEntry> AccountStore::PublicListing() const {
    std::vector<PublicEntry> entries;
    entries.reserve(accounts_.size());
    for (const auto& [id, record] : accounts_) {
        entries.push_back({id, record.displayName, record.loginUrl});
    }
    return entries;
}

}  // namespace voice_agent
