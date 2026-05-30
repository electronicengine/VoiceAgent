#include "config/AccountStore.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace voice_agent {

namespace {

std::filesystem::path ResolveRelative(
    const std::filesystem::path& base,
    const std::string& configured
) {
    const std::string trimmed = Trim(configured);
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

}  // namespace

AccountStore::AccountStore() = default;

void AccountStore::Load(
    const std::filesystem::path& accountsFile,
    const std::filesystem::path& defaultRoot
) {
    accounts_.clear();
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
        record.displayName = ReadStringField(it.value(), "displayName");
        record.provider = ToLower(ReadStringField(it.value(), "provider"));
        record.loginUrl = ReadStringField(it.value(), "loginUrl");
        record.loggedInUrl = ReadStringField(it.value(), "loggedInUrl");
        record.loginCheckSelector = ReadStringField(it.value(), "loginCheckSelector");
        record.email = ReadStringField(it.value(), "email");
        record.password = ReadStringField(it.value(), "password");

        const std::string configuredProfile = ReadStringField(it.value(), "profileDir");
        if (!configuredProfile.empty()) {
            record.profileDir = ResolveRelative(configDir, configuredProfile);
        } else {
            record.profileDir = (rootDir_ / id).lexically_normal();
        }

        accounts_.emplace(id, std::move(record));
    }
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
    const auto it = accounts_.find(Trim(id));
    if (it == accounts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<AccountStore::PublicEntry> AccountStore::PublicListing() const {
    std::vector<PublicEntry> entries;
    entries.reserve(accounts_.size());
    for (const auto& [id, record] : accounts_) {
        entries.push_back({id, record.displayName, record.provider, record.loginUrl});
    }
    return entries;
}

}  // namespace voice_agent
