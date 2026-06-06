#include "audio/AlsaMicrophone.h"

#include <alsa/asoundlib.h>
#include "common/logger.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace voice_agent {

namespace {

constexpr int kFrameMs = 10;

}  // namespace

AlsaMicrophone::AlsaMicrophone(const AppConfig& config)
    : sampleRate_(config.speechSampleRate),
      deviceName_(config.alsaCaptureDevice) {}

AlsaMicrophone::~AlsaMicrophone() {
    Stop();
}

void AlsaMicrophone::Start(FrameCallback callback) {
    if (running_.exchange(true)) {
        return;
    }
    callback_ = std::move(callback);
    thread_ = std::thread(&AlsaMicrophone::CaptureLoop, this);
}

void AlsaMicrophone::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    callback_ = nullptr;
}

void AlsaMicrophone::CaptureLoop() {
    constexpr unsigned int channelCount = 1;
    constexpr snd_pcm_format_t sampleFormat = SND_PCM_FORMAT_S16_LE;

    const std::size_t frameSamples = static_cast<std::size_t>(sampleRate_) * kFrameMs / 1000;
    const snd_pcm_uframes_t chunkFrames = static_cast<snd_pcm_uframes_t>(frameSamples);

    snd_pcm_t* pcmHandle = nullptr;
    snd_pcm_hw_params_t* hardwareParams = nullptr;
    const char* deviceName = deviceName_.empty() ? "default" : deviceName_.c_str();

    int result = snd_pcm_open(&pcmHandle, deviceName, SND_PCM_STREAM_CAPTURE, 0);
    if (result < 0) {
        ERROR("[mic] open failed: {}", snd_strerror(result));
        running_.store(false);
        return;
    }

    try {
        snd_pcm_hw_params_alloca(&hardwareParams);

        if ((result = snd_pcm_hw_params_any(pcmHandle, hardwareParams)) < 0) {
            throw std::runtime_error(std::string("hw_params_any: ") + snd_strerror(result));
        }
        if ((result = snd_pcm_hw_params_set_access(pcmHandle, hardwareParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
            throw std::runtime_error(std::string("set_access: ") + snd_strerror(result));
        }
        if ((result = snd_pcm_hw_params_set_format(pcmHandle, hardwareParams, sampleFormat)) < 0) {
            throw std::runtime_error(std::string("set_format: ") + snd_strerror(result));
        }
        if ((result = snd_pcm_hw_params_set_channels(pcmHandle, hardwareParams, channelCount)) < 0) {
            throw std::runtime_error(std::string("set_channels: ") + snd_strerror(result));
        }

        unsigned int configuredRate = static_cast<unsigned int>(sampleRate_);
        if ((result = snd_pcm_hw_params_set_rate_near(pcmHandle, hardwareParams, &configuredRate, nullptr)) < 0) {
            throw std::runtime_error(std::string("set_rate: ") + snd_strerror(result));
        }
        if (configuredRate != static_cast<unsigned int>(sampleRate_)) {
            throw std::runtime_error("ALSA capture device could not be configured to the requested sample rate.");
        }

        snd_pcm_uframes_t buffer = chunkFrames * 4;
        if ((result = snd_pcm_hw_params_set_buffer_size_near(pcmHandle, hardwareParams, &buffer)) < 0) {
            throw std::runtime_error(std::string("set_buffer_size: ") + snd_strerror(result));
        }
        if ((result = snd_pcm_hw_params(pcmHandle, hardwareParams)) < 0) {
            throw std::runtime_error(std::string("hw_params: ") + snd_strerror(result));
        }
        if ((result = snd_pcm_prepare(pcmHandle)) < 0) {
            throw std::runtime_error(std::string("prepare: ") + snd_strerror(result));
        }

        std::vector<std::int16_t> chunkBuffer(frameSamples);
        INFO("[mic] streaming capture started (sr={})", sampleRate_);

        while (running_.load()) {
            const snd_pcm_sframes_t r = snd_pcm_readi(pcmHandle, chunkBuffer.data(), chunkFrames);
            if (r == -EPIPE) {
                snd_pcm_prepare(pcmHandle);
                continue;
            }
            if (r < 0) {
                ERROR("[mic] read error: {}", snd_strerror(static_cast<int>(r)));
                snd_pcm_prepare(pcmHandle);
                continue;
            }
            if (callback_ && static_cast<std::size_t>(r) == frameSamples) {
                callback_(chunkBuffer.data(), frameSamples);
            }
        }

        snd_pcm_drop(pcmHandle);
    } catch (const std::exception& ex) {
        ERROR("[mic] capture loop error: {}", ex.what());
    }

    if (pcmHandle != nullptr) {
        snd_pcm_close(pcmHandle);
    }
}

}  // namespace voice_agent
