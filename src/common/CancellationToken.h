#pragma once

#include <atomic>
#include <memory>

namespace voice_agent {

class CancellationToken {
public:
    CancellationToken() = default;
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    void Cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool IsCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> cancelled_{false};
};

using CancellationTokenPtr = std::shared_ptr<CancellationToken>;

}  // namespace voice_agent
