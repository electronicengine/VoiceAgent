#include "speaker.h"

#include <algorithm>

#include "alsa_utils.h"

namespace robot_audio {

Speaker::Speaker(const std::string& device)
    : handle_(openAlsaDevice(device, SND_PCM_STREAM_PLAYBACK)) {}

Speaker::~Speaker() {
    if (handle_ != nullptr) {
        snd_pcm_close(handle_);
    }
}

void Speaker::playBuffer(const std::vector<int16_t>& samples,
                         const PlaybackFrameCallback& onFrame) {
    if (samples.empty()) {
        return;
    }

    size_t offset = 0;
    while (offset < samples.size()) {
        AudioBuffer frame{};
        const size_t chunk = std::min(samples.size() - offset,
                                      static_cast<size_t>(kFrameSamples));
        std::copy_n(samples.data() + offset, chunk, frame.begin());

        if (onFrame) {
            onFrame(frame);
        }

        snd_pcm_sframes_t written = snd_pcm_writei(handle_, frame.data(), frame.size());
        if (written < 0) {
            snd_pcm_recover(handle_, static_cast<int>(written), 0);
        }

        offset += chunk;
    }
}

}  // namespace robot_audio