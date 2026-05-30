#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace voice_agent {

class IMicrophone {
public:
    using FrameCallback = std::function<void(const std::int16_t* samples, std::size_t sampleCount)>;

    virtual ~IMicrophone() = default;

    // Starts a background capture thread that delivers fixed 10 ms PCM frames
    // (mono S16LE at SampleRate()) to `callback`. Throws on ALSA errors.
    virtual void Start(FrameCallback callback) = 0;

    // Stops capture and joins the worker thread.
    virtual void Stop() = 0;

    virtual int SampleRate() const = 0;
};

}  // namespace voice_agent