#pragma once

#include <atomic>

namespace up {

// Cooperative cancellation flag shared between a job and whoever may cancel it.
class CancelToken {
public:
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return cancelled_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace up
