#pragma once

#include <vector>

#include "microphone.h"
#include "speaker.h"
#include "speech_processor.h"

namespace robot_audio {

class RobotAudioController {
public:
    RobotAudioController();

    void run();

private:
    enum class State {
        Listening,
        Recording,
    };

    void handleListening(const SpeechProcessor::ProcessResult& result);
    void handleRecording(const SpeechProcessor::ProcessResult& result);
    void finalizeSpeech();

    Microphone microphone_;
    Speaker speaker_;
    SpeechProcessor processor_;
    State state_;
    std::vector<int16_t> speechBuffer_;
    std::vector<int16_t> pendingSpeech_;
    int silenceCount_;
    int speechCount_;
};

}  // namespace robot_audio