#pragma once
#include <string>

namespace voice_agent {

class IAgentInterface {
public:
    virtual ~IAgentInterface() = default;

    /**
     * @brief Called before the user text is processed by the interpreter or orchestrator.
     * Allows the interface to inject additional information (e.g., sensor data).
     * @param userText The original user text.
     * @return The potentially modified user text.
     */
    virtual std::string processUserText(const std::string& userText) = 0;

    /**
     * @brief Called when the agent produces speakable text.
     * Allows the interface to read the generated sentence and trigger external actions.
     * @param speakableText The text the agent intends to speak.
     */
    virtual void onSpeakableText(const std::string& speakableText) = 0;
};

} // namespace voice_agent
