#include "audio/AlsaSpeaker.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace voice_agent {

AlsaSpeaker::AlsaSpeaker(int sampleRate, std::string deviceName)
    : sampleRate_(sampleRate),
      deviceName_(std::move(deviceName)) {}

void AlsaSpeaker::PlayPcm16KhzMono(const std::string& pcmAudio) const {
    constexpr unsigned int channelCount = 1;
    constexpr snd_pcm_format_t sampleFormat = SND_PCM_FORMAT_S16_LE;
    constexpr snd_pcm_uframes_t chunkFrames = 1024;
    constexpr std::size_t bytesPerSample = sizeof(std::int16_t);
    constexpr std::size_t frameBytes = channelCount * bytesPerSample;

    if (pcmAudio.empty()) {
        return;
    }

    snd_pcm_t* pcmHandle = nullptr;
    snd_pcm_hw_params_t* hardwareParams = nullptr;
    const char* deviceName = deviceName_.empty() ? "default" : deviceName_.c_str();

    int result = snd_pcm_open(&pcmHandle, deviceName, SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0) {
        throw std::runtime_error(std::string("Failed to open ALSA playback device: ") + snd_strerror(result));
    }

    try {
        snd_pcm_hw_params_alloca(&hardwareParams);

        result = snd_pcm_hw_params_any(pcmHandle, hardwareParams);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to query ALSA hardware params: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_access(pcmHandle, hardwareParams, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA access mode: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_format(pcmHandle, hardwareParams, sampleFormat);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA sample format: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_channels(pcmHandle, hardwareParams, channelCount);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA channel count: ") + snd_strerror(result));
        }

        unsigned int configuredRate = static_cast<unsigned int>(sampleRate_);
        result = snd_pcm_hw_params_set_rate_near(pcmHandle, hardwareParams, &configuredRate, nullptr);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA sample rate: ") + snd_strerror(result));
        }
        if (configuredRate != static_cast<unsigned int>(sampleRate_)) {
            throw std::runtime_error("ALSA device could not be configured to the requested sample rate.");
        }

        snd_pcm_uframes_t configuredBufferFrames = chunkFrames;
        result = snd_pcm_hw_params_set_buffer_size_near(pcmHandle, hardwareParams, &configuredBufferFrames);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA buffer size: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params(pcmHandle, hardwareParams);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to apply ALSA hardware params: ") + snd_strerror(result));
        }

        const char* audioData = pcmAudio.data();
        std::size_t remainingBytes = pcmAudio.size();
        while (remainingBytes >= frameBytes) {
            const snd_pcm_uframes_t framesToWrite =
                static_cast<snd_pcm_uframes_t>(std::min<std::size_t>(chunkFrames, remainingBytes / frameBytes));

            result = snd_pcm_writei(pcmHandle, audioData, framesToWrite);
            if (result == -EPIPE) {
                result = snd_pcm_prepare(pcmHandle);
                if (result < 0) {
                    throw std::runtime_error(std::string("Failed to recover ALSA underrun: ") + snd_strerror(result));
                }
                continue;
            }
            if (result < 0) {
                throw std::runtime_error(std::string("Failed to write PCM frames to ALSA: ") + snd_strerror(result));
            }

            const std::size_t consumedBytes = static_cast<std::size_t>(result) * frameBytes;
            audioData += consumedBytes;
            remainingBytes -= consumedBytes;
        }

        if (remainingBytes != 0) {
            throw std::runtime_error("PCM audio payload is not aligned to whole frames.");
        }

        result = snd_pcm_drain(pcmHandle);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to drain ALSA playback buffer: ") + snd_strerror(result));
        }
    } catch (...) {
        snd_pcm_close(pcmHandle);
        throw;
    }

    snd_pcm_close(pcmHandle);
}

}  // namespace voice_agent