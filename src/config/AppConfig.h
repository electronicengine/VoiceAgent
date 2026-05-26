#pragma once

#include <string>

namespace voice_agent {

struct AppConfig {
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
    int maxAgentSteps = 3;
    int openAiRunPollIntervalMs = 500;
    int openAiRunPollTimeoutSeconds = 90;
    bool deepgramSmartFormat = true;
    bool deepgramPunctuate = true;
    bool deepgramDiarize = false;
    bool dangerousShellEnabled = false;
    std::string alsaCaptureDevice;
    std::string alsaPlaybackDevice;
};

AppConfig LoadConfig();

}  // namespace voice_agent