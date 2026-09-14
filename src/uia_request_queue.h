#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>

namespace uia {

// Single-slot latest-wins queue. A slow/hung provider can never create an
// unbounded backlog; a newer pointer target replaces stale pending work.
template <class Request>
class SingleSlotQueue {
public:
    void submit(Request request) {
        std::lock_guard lock(mutex_);
        pending_ = std::move(request);
    }

    std::optional<Request> take() {
        std::lock_guard lock(mutex_);
        if (!pending_) return std::nullopt;
        auto result = std::move(pending_);
        pending_.reset();
        return result;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        pending_.reset();
    }

private:
    std::mutex mutex_;
    std::optional<Request> pending_;
};

}  // namespace uia
