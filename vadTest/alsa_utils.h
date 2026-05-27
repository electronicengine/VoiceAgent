#pragma once

#include <alsa/asoundlib.h>

#include <stdexcept>
#include <string>

#include "audio_config.h"

namespace robot_audio {

inline snd_pcm_t* openAlsaDevice(const std::string& device, snd_pcm_stream_t direction) {
    snd_pcm_t* handle = nullptr;
    int err = snd_pcm_open(&handle, device.c_str(), direction, 0);
    if (err < 0) {
        throw std::runtime_error("ALSA acma hatasi (" + device + "): " +
                                 snd_strerror(err));
    }

    err = snd_pcm_set_params(handle,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             1,
                             kSampleRate,
                             1,
                             kFrameMs * 1000);
    if (err < 0) {
        const std::string message = "ALSA parametre hatasi (" + device + "): " +
                                    snd_strerror(err);
        snd_pcm_close(handle);
        throw std::runtime_error(message);
    }

    return handle;
}

}  // namespace robot_audio