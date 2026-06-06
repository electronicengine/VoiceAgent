#pragma once

#include <string>

namespace voice_agent {

struct AppConfig {
    std::string agentMode;
    std::string transcriberProvider;
    std::string interpreterProvider;
    std::string synthesizerProvider;
    std::string azureSpeechKey;
    std::string azureSpeechRegion;
    std::string deepgramApiKey;
    std::string deepgramBaseUrl;
    std::string deepgramModel;
    std::string openAiApiKey;
    std::string openAiBaseUrl;
    std::string openAiModel;
    std::string openAiAssistantId;
    std::string systemPromptFilePath;
    std::string resolvedSystemPromptFilePath;
    std::string speechLanguage;
    std::string voiceName;
    std::string systemPromptText;
    int speechSampleRate = 16000;
    int captureDurationSeconds = 6;
    bool vadEnabled = true;
    int vadFrameMs = 20;
    int vadStartSpeechMs = 200;
    int vadEndSilenceMs = 800;
    int vadMaxCaptureMs = 25000;
    int vadPreRollMs = 200;
    int vadAmplitudeThreshold = 900;
    int vadPlaybackCooldownMs = 200;
    int aecStreamDelayMs = 40;
    int maxAgentSteps =10;
    int openAiRunPollIntervalMs = 500;
    int openAiRunPollTimeoutSeconds = 90;
    bool deepgramSmartFormat = true;
    bool deepgramPunctuate = true;
    bool deepgramDiarize = false;
    bool dangerousShellEnabled = false;
    std::string alsaCaptureDevice;
    std::string alsaPlaybackDevice;
    std::string pythonToolScriptRoot;
    std::string resolvedPythonToolScriptRoot;
    std::string pythonWebRunnerPath;
    std::string resolvedPythonWebRunnerPath;
    std::string accountsFilePath;
    std::string resolvedAccountsFilePath;
    std::string accountsRootDir;
    std::string resolvedAccountsRootDir;
    int browserPromptTimeoutSeconds = 180;

    // Llama / Embeddings
    std::string llamaEmbedModelPath;

    // Skill / experience system
    bool skillsEnabled = true;
    std::string skillsDir;
    std::string resolvedSkillsDir;
    std::string experiencesFilePath;
    std::string resolvedExperiencesFilePath;
    std::string experiencesText;
    int maxExperienceLines = 100;
    int maxSkillsPerTurn = 3;
};

AppConfig LoadConfig();

}  // namespace voice_agent