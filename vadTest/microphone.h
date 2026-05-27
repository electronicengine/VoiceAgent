#pragma once

#include <alsa/asoundlib.h>

#include <string>

#include "audio_config.h"

namespace robot_audio {

class Microphone {
public:
    explicit Microphone(const std::string& device = "default");
    ~Microphone();

    void readFrame(AudioBuffer& frame);

private:
    snd_pcm_t* handle_;
};

}  // namespace robot_audio