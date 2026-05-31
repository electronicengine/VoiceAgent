#include "config/AppConfig.h"

#include "common/StringUtils.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace voice_agent {

namespace {

std::string GetEnv(const char* name, const char* fallback = "") {
#if defined(_MSC_VER)
    size_t requiredSize = 0;
    (void)getenv_s(&requiredSize, nullptr, 0, name);
    if (requiredSize == 0) {
        return fallback;
    }
    std::string value(requiredSize, '\0');
    (void)getenv_s(&requiredSize, value.data(), requiredSize, name);
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
#else
    const char* value = std::getenv(name);
    return value ? value : fallback;
#endif
}

int ParsePositiveInt(const char* name, int fallback) {
    const std::string value = Trim(GetEnv(name));
    if (value.empty()) {
        return fallback;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0) {
            throw std::runtime_error("must be positive");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(name) + " must be a positive integer.");
    }
}

int ParsePositiveIntValue(const std::string& value, const char* fieldName) {
    try {
        const int parsed = std::stoi(Trim(value));
        if (parsed <= 0) {
            throw std::runtime_error("must be positive");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(fieldName) + " must be a positive integer.");
    }
}

std::filesystem::path ResolveConfigPath() {
    const std::string configuredPath = Trim(GetEnv("VOICE_AGENT_CONFIG"));
    if (!configuredPath.empty()) {
        return configuredPath;
    }

    const std::filesystem::path currentPath = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        currentPath / "voiceAgentConfig.json",
        currentPath / "../voiceAgentConfig.json",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    std::ostringstream message;
    message << "Config file not found. Set VOICE_AGENT_CONFIG or create one of: ";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            message << ", ";
        }
        message << candidates[index].lexically_normal().string();
    }

    throw std::runtime_error(message.str());
}

std::filesystem::path ResolveOptionalPath(
    const std::filesystem::path& configPath,
    const std::string& configuredPath
) {
    const std::string trimmedPath = Trim(configuredPath);
    if (trimmedPath.empty()) {
        return {};
    }

    const std::filesystem::path path(trimmedPath);
    if (path.is_absolute()) {
        return path;
    }

    return (configPath.parent_path() / path).lexically_normal();
}

std::string LoadTextFile(const std::filesystem::path& filePath, const char* description) {
    std::ifstream file(filePath);
    if (!file) {
        throw std::runtime_error(std::string("Failed to open ") + description + ": " + filePath.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = Trim(buffer.str());
    if (content.empty()) {
        throw std::runtime_error(std::string(description) + " cannot be empty: " + filePath.string());
    }
    return content;
}

const nlohmann::json& LoadJsonConfig(const std::filesystem::path& configPath) {
    static nlohmann::json jsonConfig;
    static std::filesystem::path loadedFrom;

    if (loadedFrom == configPath) {
        return jsonConfig;
    }

    std::ifstream file(configPath);
    if (!file) {
        throw std::runtime_error("Failed to open config file: " + configPath.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    try {
        jsonConfig = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const nlohmann::json::exception& ex) {
        throw std::runtime_error(
            "Failed to parse config file '" + configPath.string() + "': " + ex.what()
        );
    }

    if (!jsonConfig.is_object()) {
        throw std::runtime_error("Config file must contain a JSON object: " + configPath.string());
    }

    loadedFrom = configPath;
    return jsonConfig;
}

std::string ReadStringField(
    const nlohmann::json& configJson,
    const char* fieldName,
    const std::string& fallback = ""
) {
    const auto iterator = configJson.find(fieldName);
    if (iterator == configJson.end() || iterator->is_null()) {
        return fallback;
    }
    if (!iterator->is_string()) {
        throw std::runtime_error(std::string(fieldName) + " must be a string.");
    }
    return iterator->get<std::string>();
}

int ReadPositiveIntField(const nlohmann::json& configJson, const char* fieldName, int fallback) {
    const auto iterator = configJson.find(fieldName);
    if (iterator == configJson.end() || iterator->is_null()) {
        return fallback;
    }
    if (iterator->is_number_integer()) {
        const int parsed = iterator->get<int>();
        if (parsed <= 0) {
            throw std::runtime_error(std::string(fieldName) + " must be a positive integer.");
        }
        return parsed;
    }
    if (iterator->is_string()) {
        return ParsePositiveIntValue(iterator->get<std::string>(), fieldName);
    }
    throw std::runtime_error(std::string(fieldName) + " must be a positive integer.");
}

bool ReadBoolField(const nlohmann::json& configJson, const char* fieldName, bool fallback) {
    const auto iterator = configJson.find(fieldName);
    if (iterator == configJson.end() || iterator->is_null()) {
        return fallback;
    }
    if (!iterator->is_boolean()) {
        throw std::runtime_error(std::string(fieldName) + " must be a boolean.");
    }
    return iterator->get<bool>();
}

const nlohmann::json* ReadSection(const nlohmann::json& configJson, const char* sectionName) {
    const auto iterator = configJson.find(sectionName);
    if (iterator == configJson.end() || iterator->is_null()) {
        return nullptr;
    }
    if (!iterator->is_object()) {
        throw std::runtime_error(std::string(sectionName) + " must be an object.");
    }
    return &(*iterator);
}

const nlohmann::json* ReadSection(const nlohmann::json* configJson, const char* sectionName) {
    if (configJson == nullptr) {
        return nullptr;
    }
    return ReadSection(*configJson, sectionName);
}

std::string ReadStringField(
    const nlohmann::json* sectionJson,
    const nlohmann::json& legacyConfigJson,
    const char* fieldName,
    const std::string& fallback = ""
) {
    if (sectionJson != nullptr) {
        return ReadStringField(*sectionJson, fieldName, ReadStringField(legacyConfigJson, fieldName, fallback));
    }
    return ReadStringField(legacyConfigJson, fieldName, fallback);
}

int ReadPositiveIntField(
    const nlohmann::json* sectionJson,
    const nlohmann::json& legacyConfigJson,
    const char* fieldName,
    int fallback
) {
    if (sectionJson != nullptr) {
        return ReadPositiveIntField(
            *sectionJson,
            fieldName,
            ReadPositiveIntField(legacyConfigJson, fieldName, fallback)
        );
    }
    return ReadPositiveIntField(legacyConfigJson, fieldName, fallback);
}

bool ReadBoolField(
    const nlohmann::json* sectionJson,
    const nlohmann::json& legacyConfigJson,
    const char* fieldName,
    bool fallback
) {
    if (sectionJson != nullptr) {
        return ReadBoolField(*sectionJson, fieldName, ReadBoolField(legacyConfigJson, fieldName, fallback));
    }
    return ReadBoolField(legacyConfigJson, fieldName, fallback);
}

}  // namespace

AppConfig LoadConfig() {
    const std::filesystem::path configPath = ResolveConfigPath();
    const nlohmann::json& configJson = LoadJsonConfig(configPath);
    const nlohmann::json* commonJson = ReadSection(configJson, "common");
    const nlohmann::json* transcriberJson = ReadSection(configJson, "transcriber");
    const nlohmann::json* interpreterJson = ReadSection(configJson, "interpreter");
    const nlohmann::json* synthesizerJson = ReadSection(configJson, "synthesizer");
    const nlohmann::json* transcriberAzureJson = ReadSection(transcriberJson, "azure");
    const nlohmann::json* transcriberDeepgramJson = ReadSection(transcriberJson, "deepgram");
    const nlohmann::json* interpreterOpenAiJson = ReadSection(interpreterJson, "openai");
    const nlohmann::json* synthesizerAzureJson = ReadSection(synthesizerJson, "azure");

    AppConfig config;
    config.agentMode = ToLower(ReadStringField(commonJson, configJson, "agentMode", "voice"));
    config.transcriberProvider = ToLower(
        ReadStringField(transcriberJson, configJson, "provider", ReadStringField(configJson, "transcriberProvider", "azure"))
    );
    config.interpreterProvider = ToLower(
        ReadStringField(interpreterJson, configJson, "provider", "openai")
    );
    config.synthesizerProvider = ToLower(
        ReadStringField(synthesizerJson, configJson, "provider", ReadStringField(configJson, "synthesizerProvider", "azure"))
    );

    const std::string transcriberAzureSpeechKey = ReadStringField(
        transcriberAzureJson,
        configJson,
        "azureSpeechKey"
    );
    const std::string synthesizerAzureSpeechKey = ReadStringField(
        synthesizerAzureJson,
        configJson,
        "azureSpeechKey"
    );
    config.azureSpeechKey = !transcriberAzureSpeechKey.empty() ? transcriberAzureSpeechKey : synthesizerAzureSpeechKey;

    const std::string transcriberAzureSpeechRegion = ReadStringField(
        transcriberAzureJson,
        configJson,
        "azureSpeechRegion"
    );
    const std::string synthesizerAzureSpeechRegion = ReadStringField(
        synthesizerAzureJson,
        configJson,
        "azureSpeechRegion"
    );
    config.azureSpeechRegion = !transcriberAzureSpeechRegion.empty() ? transcriberAzureSpeechRegion : synthesizerAzureSpeechRegion;

    config.deepgramApiKey = ReadStringField(transcriberDeepgramJson, configJson, "deepgramApiKey");
    config.deepgramBaseUrl = ReadStringField(
        transcriberDeepgramJson,
        configJson,
        "deepgramBaseUrl",
        "https://api.deepgram.com"
    );
    config.deepgramModel = ReadStringField(transcriberDeepgramJson, configJson, "deepgramModel", "nova-3");
    config.openAiApiKey = ReadStringField(interpreterOpenAiJson, configJson, "openAiApiKey");
    config.openAiBaseUrl = ReadStringField(
        interpreterOpenAiJson,
        configJson,
        "openAiBaseUrl",
        "https://api.openai.com/v1"
    );
    config.openAiModel = ReadStringField(interpreterOpenAiJson, configJson, "openAiModel", "gpt-4o");
    config.openAiAssistantId = ReadStringField(interpreterOpenAiJson, configJson, "openAiAssistantId");
    config.systemPromptFilePath = ReadStringField(
        interpreterOpenAiJson,
        configJson,
        "systemPromptFilePath"
    );
    config.speechLanguage = ReadStringField(
        synthesizerAzureJson,
        configJson,
        "speechLanguage",
        ReadStringField(commonJson, "speechLanguage", "tr-TR")
    );
    config.voiceName = ReadStringField(
        synthesizerAzureJson,
        configJson,
        "voiceName",
        "en-US-DustinMultilingualNeural"
    );
    config.speechSampleRate = ReadPositiveIntField(
        synthesizerAzureJson,
        configJson,
        "speechSampleRate",
        commonJson != nullptr ? ReadPositiveIntField(*commonJson, "speechSampleRate", 16000) : 16000
    );
    config.captureDurationSeconds = ReadPositiveIntField(
        transcriberJson,
        configJson,
        "captureDurationSeconds",
        commonJson != nullptr ? ReadPositiveIntField(*commonJson, "captureDurationSeconds", 6) : 6
    );
    config.vadEnabled = ReadBoolField(transcriberJson, configJson, "vadEnabled", true);
    config.vadFrameMs = ReadPositiveIntField(transcriberJson, configJson, "vadFrameMs", 20);
    config.vadStartSpeechMs = ReadPositiveIntField(transcriberJson, configJson, "vadStartSpeechMs", 200);
    config.vadEndSilenceMs = ReadPositiveIntField(transcriberJson, configJson, "vadEndSilenceMs", 800);
    config.vadMaxCaptureMs = ReadPositiveIntField(transcriberJson, configJson, "vadMaxCaptureMs", 25000);
    config.vadPreRollMs = ReadPositiveIntField(transcriberJson, configJson, "vadPreRollMs", 200);
    config.vadAmplitudeThreshold = ReadPositiveIntField(
        transcriberJson,
        configJson,
        "vadAmplitudeThreshold",
        900
    );
    config.vadPlaybackCooldownMs = ReadPositiveIntField(
        transcriberJson,
        configJson,
        "vadPlaybackCooldownMs",
        200
    );
    config.aecStreamDelayMs = ReadPositiveIntField(
        transcriberJson,
        configJson,
        "aecStreamDelayMs",
        40
    );
    config.maxAgentSteps = ReadPositiveIntField(commonJson, configJson, "maxAgentSteps", 3);
    config.openAiRunPollIntervalMs = ReadPositiveIntField(
        interpreterOpenAiJson,
        configJson,
        "openAiRunPollIntervalMs",
        500
    );
    config.openAiRunPollTimeoutSeconds = ReadPositiveIntField(
        interpreterOpenAiJson,
        configJson,
        "openAiRunPollTimeoutSeconds",
        90
    );
    config.deepgramSmartFormat = ReadBoolField(transcriberDeepgramJson, configJson, "deepgramSmartFormat", true);
    config.deepgramPunctuate = ReadBoolField(transcriberDeepgramJson, configJson, "deepgramPunctuate", true);
    config.deepgramDiarize = ReadBoolField(transcriberDeepgramJson, configJson, "deepgramDiarize", false);
    config.dangerousShellEnabled = ReadBoolField(commonJson, configJson, "dangerousShellEnabled", false);
    config.alsaCaptureDevice = ReadStringField(commonJson, configJson, "alsaCaptureDevice");
    config.alsaPlaybackDevice = ReadStringField(commonJson, configJson, "alsaPlaybackDevice");
    config.webBrowserRunnerPath = ReadStringField(configJson, "webBrowserRunnerPath", "src/tools/webbrowser_runner.py");
    config.accountsFilePath = ReadStringField(configJson, "accountsFilePath", "account.json");
    config.accountsRootDir = ReadStringField(configJson, "accountsRootDir", ".voice_agent_browser/accounts");
    config.browserPromptTimeoutSeconds = ReadPositiveIntField(
        configJson, "browserPromptTimeoutSeconds", 180
    );

    {
        const nlohmann::json* skillsJson = ReadSection(configJson, "skills");
        config.skillsEnabled = ReadBoolField(skillsJson, configJson, "skillsEnabled", true);
        config.skillsDir = ReadStringField(skillsJson, configJson, "skillsDir", "skills");
        config.experiencesFilePath = ReadStringField(
            skillsJson, configJson, "experiencesFilePath", "experiences.md"
        );
        config.maxExperienceLines = ReadPositiveIntField(
            skillsJson, configJson, "maxExperienceLines", 100
        );
        config.maxSkillsPerTurn = ReadPositiveIntField(
            skillsJson, configJson, "maxSkillsPerTurn", 3
        );
    }

    {
        const std::filesystem::path resolvedAccountsFile = ResolveOptionalPath(configPath, config.accountsFilePath);
        if (!resolvedAccountsFile.empty()) {
            config.resolvedAccountsFilePath = resolvedAccountsFile.string();
        }
        const std::filesystem::path resolvedAccountsRoot = ResolveOptionalPath(configPath, config.accountsRootDir);
        if (!resolvedAccountsRoot.empty()) {
            config.resolvedAccountsRootDir = resolvedAccountsRoot.string();
        }
        const std::filesystem::path resolvedSkills = ResolveOptionalPath(configPath, config.skillsDir);
        if (!resolvedSkills.empty()) {
            config.resolvedSkillsDir = resolvedSkills.string();
        }
        const std::filesystem::path resolvedExperiences = ResolveOptionalPath(configPath, config.experiencesFilePath);
        if (!resolvedExperiences.empty()) {
            config.resolvedExperiencesFilePath = resolvedExperiences.string();
            std::ifstream file(resolvedExperiences);
            if (file) {
                std::ostringstream buf;
                buf << file.rdbuf();
                config.experiencesText = Trim(buf.str());
            }
        }
    }

    if (!transcriberAzureSpeechKey.empty() && !synthesizerAzureSpeechKey.empty() &&
        transcriberAzureSpeechKey != synthesizerAzureSpeechKey) {
        throw std::runtime_error(
            "transcriber.azure.azureSpeechKey and synthesizer.azure.azureSpeechKey must match."
        );
    }
    if (!transcriberAzureSpeechRegion.empty() && !synthesizerAzureSpeechRegion.empty() &&
        transcriberAzureSpeechRegion != synthesizerAzureSpeechRegion) {
        throw std::runtime_error(
            "transcriber.azure.azureSpeechRegion and synthesizer.azure.azureSpeechRegion must match."
        );
    }

    if (config.agentMode != "voice" && config.agentMode != "text") {
        throw std::runtime_error("common.agentMode must be either 'voice' or 'text'.");
    }
    if (config.transcriberProvider != "azure" && config.transcriberProvider != "deepgram") {
        throw std::runtime_error("transcriber.provider must be either 'azure' or 'deepgram'.");
    }
    if (config.interpreterProvider != "openai") {
        throw std::runtime_error("interpreter.provider must be 'openai'.");
    }
    if (config.synthesizerProvider != "azure") {
        throw std::runtime_error("synthesizer.provider must be 'azure'.");
    }
    if (config.transcriberProvider == "azure") {
        if (config.azureSpeechKey.empty()) {
            throw std::runtime_error("azureSpeechKey is required when transcriber.provider is 'azure'.");
        }
        if (config.azureSpeechRegion.empty()) {
            throw std::runtime_error("azureSpeechRegion is required when transcriber.provider is 'azure'.");
        }
        if (config.speechSampleRate != 16000) {
            throw std::runtime_error("speechSampleRate must be 16000 for the current Azure REST audio format.");
        }
    }
    if (config.vadFrameMs > config.vadMaxCaptureMs) {
        throw std::runtime_error("vadFrameMs must be less than or equal to vadMaxCaptureMs.");
    }
    if (config.vadStartSpeechMs > config.vadMaxCaptureMs) {
        throw std::runtime_error("vadStartSpeechMs must be less than or equal to vadMaxCaptureMs.");
    }
    if (config.vadEndSilenceMs > config.vadMaxCaptureMs) {
        throw std::runtime_error("vadEndSilenceMs must be less than or equal to vadMaxCaptureMs.");
    }
    if (config.vadPreRollMs > config.vadMaxCaptureMs) {
        throw std::runtime_error("vadPreRollMs must be less than or equal to vadMaxCaptureMs.");
    }
    if (config.transcriberProvider == "deepgram" && config.deepgramApiKey.empty()) {
        throw std::runtime_error("deepgramApiKey is required when transcriber.provider is 'deepgram'.");
    }
    if (config.synthesizerProvider == "azure") {
        if (config.azureSpeechKey.empty()) {
            throw std::runtime_error("azureSpeechKey is required when synthesizer.provider is 'azure'.");
        }
        if (config.azureSpeechRegion.empty()) {
            throw std::runtime_error("azureSpeechRegion is required when synthesizer.provider is 'azure'.");
        }
        if (config.speechSampleRate != 16000) {
            throw std::runtime_error("speechSampleRate must be 16000 for the current Azure REST audio format.");
        }
    }
    if (config.openAiApiKey.empty()) {
        throw std::runtime_error("openAiApiKey is required in the JSON config.");
    }

    const std::filesystem::path promptPath = ResolveOptionalPath(configPath, config.systemPromptFilePath);
    if (promptPath.empty()) {
        config.systemPromptText = ReadStringField(
            interpreterOpenAiJson,
            configJson,
            "systemPrompt",
            "Sen sesli bir asistansin. Kisa, dogrudan ve yardimci yanit ver."
        );
    } else {
        config.resolvedSystemPromptFilePath = promptPath.string();
        config.systemPromptText = LoadTextFile(promptPath, "system prompt file");
    }

    return config;
}

}  // namespace voice_agent