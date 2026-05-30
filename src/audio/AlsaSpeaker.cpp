#include "audio/AlsaSpeaker.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace voice_agent {

namespace {

constexpr int kFrameMs = 10;

}  // namespace

AlsaSpeaker::AlsaSpeaker(int sampleRate, std::string deviceName)
    : sampleRate_(sampleRate),
      deviceName_(std::move(deviceName)) {}

AlsaSpeaker::~AlsaSpeaker() {
    Shutdown();
}

void AlsaSpeaker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&AlsaSpeaker::WorkerLoop, this);
}

void AlsaSpeaker::Shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        abortCurrent_.store(true);
    }
    cv_.notify_all();
    idleCv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AlsaSpeaker::Enqueue(std::string pcm) {
    if (pcm.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(pcm));
    }
    cv_.notify_all();
}

void AlsaSpeaker::StopPlayback() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        abortCurrent_.store(true);
    }
    cv_.notify_all();
    idleCv_.notify_all();
}

void AlsaSpeaker::WaitUntilIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this] {
        return !running_.load() || (queue_.empty() && !writing_.load());
    });
}

bool AlsaSpeaker::IsActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writing_.load() || !queue_.empty();
}

void AlsaSpeaker::SetFrameSink(FrameSink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    frameSink_ = std::move(sink);
}

void AlsaSpeaker::WorkerLoop() {
    constexpr unsigned int channelCount = 1;
    constexpr snd_pcm_format_t sampleFormat = SND_PCM_FORMAT_S16_LE;
    const std::size_t frameSamples = static_cast<std::size_t>(sampleRate_) * kFrameMs / 1000;
    const std::size_t frameBytes = frameSamples * sizeof(std::int16_t);

    snd_pcm_t* pcmHandle = nullptr;
    snd_pcm_hw_params_t* hardwareParams = nullptr;
    const char* deviceName = deviceName_.empty() ? "default" : deviceName_.c_str();

    int result = snd_pcm_open(&pcmHandle, deviceName, SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0) {
        std::cerr << "[speaker] open failed: " << snd_strerror(result) << "\n";
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
        snd_pcm_uframes_t buffer = static_cast<snd_pcm_uframes_t>(frameSamples) * 20;  // ~200 ms
        snd_pcm_hw_params_set_buffer_size_near(pcmHandle, hardwareParams, &buffer);
        snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(frameSamples) * 4;  // ~40 ms
        snd_pcm_hw_params_set_period_size_near(pcmHandle, hardwareParams, &period, nullptr);
        if ((result = snd_pcm_hw_params(pcmHandle, hardwareParams)) < 0) {
            throw std::runtime_error(std::string("hw_params: ") + snd_strerror(result));
        }

        std::vector<char> leftover;
        bool needsPrepare = true;

        while (running_.load()) {
            std::string chunk;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !running_.load() || !queue_.empty(); });
                if (!running_.load()) {
                    break;
                }
                chunk = std::move(queue_.front());
                queue_.pop_front();
                writing_.store(true);
                abortCurrent_.store(false);
            }

            // Combine with leftover from previous (sub-frame) chunk to keep frames aligned.
            std::vector<char> data;
            data.reserve(leftover.size() + chunk.size());
            data.insert(data.end(), leftover.begin(), leftover.end());
            data.insert(data.end(), chunk.begin(), chunk.end());
            leftover.clear();

            const std::size_t totalFrames = data.size() / frameBytes;
            const std::size_t consumedBytes = totalFrames * frameBytes;
            if (data.size() > consumedBytes) {
                leftover.assign(data.begin() + static_cast<std::ptrdiff_t>(consumedBytes), data.end());
            }

            // Only (re)prepare the device at startup or after an underrun/abort.
            if (needsPrepare) {
                snd_pcm_prepare(pcmHandle);
                needsPrepare = false;
            }

            std::size_t cursor = 0;
            while (cursor + frameBytes <= consumedBytes && running_.load() && !abortCurrent_.load()) {
                const auto* framePtr = reinterpret_cast<const std::int16_t*>(data.data() + cursor);

                FrameSink sinkCopy;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    sinkCopy = frameSink_;
                }
                if (sinkCopy) {
                    sinkCopy(framePtr, frameSamples);
                }

                snd_pcm_sframes_t r = snd_pcm_writei(pcmHandle, framePtr,
                                                    static_cast<snd_pcm_uframes_t>(frameSamples));
                if (r == -EPIPE) {
                    snd_pcm_prepare(pcmHandle);
                    continue;
                }
                if (r < 0) {
                    std::cerr << "[speaker] write error: " << snd_strerror(static_cast<int>(r)) << "\n";
                    snd_pcm_prepare(pcmHandle);
                    continue;
                }
                cursor += static_cast<std::size_t>(r) * sizeof(std::int16_t);
            }

            if (abortCurrent_.load()) {
                snd_pcm_drop(pcmHandle);
                leftover.clear();
                needsPrepare = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    queue_.clear();
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                writing_.store(false);
                if (queue_.empty() && leftover.empty()) {
                    idleCv_.notify_all();
                }
            }
        }

        snd_pcm_drop(pcmHandle);
    } catch (const std::exception& ex) {
        std::cerr << "[speaker] worker error: " << ex.what() << "\n";
    }

    if (pcmHandle != nullptr) {
        snd_pcm_close(pcmHandle);
    }
    running_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        writing_.store(false);
    }
    idleCv_.notify_all();
}

}  // namespace voice_agent
