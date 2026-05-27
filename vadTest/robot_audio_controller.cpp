#include "robot_audio_controller.h"

#include <iostream>

namespace robot_audio {

RobotAudioController::RobotAudioController()
    : microphone_(),
      speaker_(),
      processor_(),
      state_(State::Listening),
      silenceCount_(0),
      speechCount_(0) {}

void RobotAudioController::run() {
    std::cout << "═══════════════════════════════════\n"
              << "  Robot Ses Dinleyici Basladi\n"
              << "  Ctrl+C ile dur\n"
              << "═══════════════════════════════════\n"
              << "[ Dinleniyor... ]\n";

    AudioBuffer microphoneFrame{};
    while (true) {
        microphone_.readFrame(microphoneFrame);
        const auto result = processor_.processCaptureFrame(microphoneFrame, false);

        if (result.becameReady) {
            std::cout << "[ VAD hazir | gurultu tabani RMS="
                      << result.noiseFloorRms << " ]\n";
        }

        if (!result.ready) {
            continue;
        }

        if (state_ == State::Listening) {
            handleListening(result);
        } else {
            handleRecording(result);
        }
    }
}

void RobotAudioController::handleListening(const SpeechProcessor::ProcessResult& result) {
    if (result.speechDetected) {
        ++speechCount_;
        pendingSpeech_.insert(pendingSpeech_.end(),
                              result.cleanedFrame.begin(),
                              result.cleanedFrame.end());
    } else {
        speechCount_ = 0;
        pendingSpeech_.clear();
    }

    if (speechCount_ >= kStartSpeechFrames) {
        std::cout << "[ Konusma basladi! ]\n";
        state_ = State::Recording;
        speechBuffer_ = pendingSpeech_;
        pendingSpeech_.clear();
        silenceCount_ = 0;
        speechCount_ = 0;
    }
}

void RobotAudioController::handleRecording(const SpeechProcessor::ProcessResult& result) {
    speechBuffer_.insert(speechBuffer_.end(),
                         result.cleanedFrame.begin(),
                         result.cleanedFrame.end());

    if (!result.speechDetected) {
        ++silenceCount_;
        if (silenceCount_ >= kEndSilenceFrames) {
            finalizeSpeech();
        }
        return;
    }

    silenceCount_ = 0;
}

void RobotAudioController::finalizeSpeech() {
    const int totalFrames = static_cast<int>(speechBuffer_.size()) / kFrameSamples;
    const int speechFrames = totalFrames - kEndSilenceFrames;

    if (speechFrames >= kMinSpeechFrames) {
        const int durationMs = speechFrames * kFrameMs;
        std::cout << "[ Konusma bitti: " << durationMs
                  << "ms - Hoparlore veriliyor... ]\n";
        speaker_.playBuffer(speechBuffer_, [this](const AudioBuffer& frame) {
            processor_.processReverseFrame(frame);
        });
        processor_.beginPlaybackCooldown();
    } else {
        std::cout << "[ Cok kisa, yoksayildi ]\n";
    }

    state_ = State::Listening;
    speechBuffer_.clear();
    pendingSpeech_.clear();
    silenceCount_ = 0;
    speechCount_ = 0;
    std::cout << "[ Dinleniyor... ]\n";
}

}  // namespace robot_audio