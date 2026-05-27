#include "microphone.h"

#include "alsa_utils.h"

namespace robot_audio {

Microphone::Microphone(const std::string& device)
    : handle_(openAlsaDevice(device, SND_PCM_STREAM_CAPTURE)) {}

Microphone::~Microphone() {
    if (handle_ != nullptr) {
        snd_pcm_close(handle_);
    }
}

void Microphone::readFrame(AudioBuffer& frame) {
    while (true) {
        snd_pcm_sframes_t readFrames = snd_pcm_readi(handle_, frame.data(), frame.size());
        if (readFrames >= 0) {
            return;
        }
        snd_pcm_recover(handle_, static_cast<int>(readFrames), 0);
    }
}

}  // namespace robot_audio