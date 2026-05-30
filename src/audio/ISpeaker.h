#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace voice_agent {

class ISpeaker {
public:
    using FrameSink = std::function<void(const std::int16_t* samples, std::size_t sampleCount)>;

    virtual ~ISpeaker() = default;

    // Start the playback worker thread (idle until Enqueue).
    virtual void Start() = 0;

    // Stop worker, drop pending PCM, abort any active write.
    virtual void Shutdown() = 0;

    // Append PCM bytes (mono S16LE at SampleRate()) to the playback queue.
    // Non-blocking: playback runs on the worker thread.
    virtual void Enqueue(std::string pcm) = 0;

    // Drop pending PCM and abort current write immediately.
    virtual void StopPlayback() = 0;

    // Block until the queue is drained (or StopPlayback is called).
    virtual void WaitUntilIdle() = 0;

    // True while there is PCM in the queue or being written.
    virtual bool IsActive() const = 0;

    // Set a sink invoked with each 10 ms frame *just before* it is written to ALSA;
    // intended to feed the AEC's reverse stream.
    virtual void SetFrameSink(FrameSink sink) = 0;

    virtual int SampleRate() const = 0;
};

}  // namespace voice_agent