#pragma once

#include <atomic>
#include <iostream>
#include <memory>

namespace voice_agent {

class CancellationToken {
public:
    CancellationToken() = default;
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    void Cancel() noexcept {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            std::cout << "[CancellationToken] Cancel requested.\n";
        }
    }
    bool IsCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> cancelled_{false};
};

using CancellationTokenPtr = std::shared_ptr<CancellationToken>;

}  // namespace voice_agent
