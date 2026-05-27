#pragma once

#include <fvad.h>
#include <webrtc_audio_processing/webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc_audio_processing/webrtc/modules/interface/module_common_types.h>

#include "audio_config.h"

namespace robot_audio {

class SpeechProcessor {
public:
    struct ProcessResult {
        AudioBuffer cleanedFrame{};
        bool ready = false;
        bool becameReady = false;
        bool speechDetected = false;
        float noiseFloorRms = kMinRms;
        float rms = 0.0f;
        float threshold = kMinRms;
    };

    SpeechProcessor();
    ~SpeechProcessor();

    void processReverseFrame(const AudioBuffer& frame);
    void beginPlaybackCooldown();
    ProcessResult processCaptureFrame(const AudioBuffer& microphoneFrame,
                                      bool detectionSuppressed);

private:
    webrtc::AudioProcessing* apm_;
    Fvad* vad_;
    webrtc::AudioFrame micFrame_;
    webrtc::AudioFrame reverseFrame_;
    int warmupFrames_;
    int playbackCooldownFrames_;
    float noiseFloorRms_;
    float rmsAccum_;
    int rmsCount_;
};

}  // namespace robot_audio