#pragma once

#include <alsa/asoundlib.h>

#include <functional>
#include <string>
#include <vector>

#include "audio_config.h"

namespace robot_audio {

using PlaybackFrameCallback = std::function<void(const AudioBuffer&)>;

class Speaker {
public:
    explicit Speaker(const std::string& device = "default");
    ~Speaker();

    void playBuffer(const std::vector<int16_t>& samples,
                    const PlaybackFrameCallback& onFrame = PlaybackFrameCallback());

private:
    snd_pcm_t* handle_;
};

}  // namespace robot_audio