#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "types.h"

// Phase Two dwell coordinator.
//
// Extends Phase One with the architecture's stable-hover *identity*: on a
// qualified dwell it resolves the HWND and a stable UIA element hash for the
// element under the cursor (off the UI thread, in an STA worker) and reports a
// full HoverTarget. A monotonic `generation` counter arbitrates stale work: any
// cursor movement bumps the generation, so a slow identity probe from a previous
// hover is discarded when it finishes.
class DwellCoordinator {
public:
    // `onDwell` receives the resolved target and the generation it belongs to.
    // `onCancel` fires when a pending/fired hover is broken by movement/hide.
    using DwellCallback = std::function<void(const HoverTarget&, uint64_t)>;
    using CancelCallback = std::function<void()>;

    DwellCoordinator(std::chrono::milliseconds dwell,
                     std::chrono::milliseconds sampleInterval,
                     int stabilityRadius,
                     DwellCallback onDwell,
                     CancelCallback onCancel)
        : dwell_(dwell),
          sampleInterval_(sampleInterval),
          radius_(stabilityRadius) {
        shared_ = std::make_shared<Shared>();
        shared_->onDwell = std::move(onDwell);
        shared_->onCancel = std::move(onCancel);
    }

    ~DwellCoordinator() {
        if (shared_) shared_->alive = false;  // detached workers exit on check
    }

    // Feed the current cursor position on every timer tick.
    void sample(POINT point);

    // Force-cancel any pending/fired dwell (e.g. on explicit hide).
    void cancel();

    // Telemetry (Phase Two: request cancellation + counts).
    uint64_t dwellCount() const { return telemetry_.dwellFired.load(); }
    uint64_t identityFailCount() const {
        return telemetry_.identityFail.load();
    }
    uint64_t cancelCount() const { return telemetry_.canceled.load(); }

private:
    bool isStable(POINT a, POINT b) const;

    enum class State { Idle, Pending, Fired };

public:
    // Shared state handed to detached worker threads. Public so the free
    // resolveIdentityAndReport() helper can receive it by shared_ptr without
    // friending; it carries only atomics + the callbacks.
    struct Shared {
        std::atomic<uint64_t> generation{0};
        std::atomic<bool> alive{true};
        DwellCallback onDwell;
        CancelCallback onCancel;
    };

    struct Telemetry {
        std::atomic<uint64_t> dwellFired{0};
        std::atomic<uint64_t> identityFail{0};
        std::atomic<uint64_t> canceled{0};
    };

    std::chrono::milliseconds dwell_;
    std::chrono::milliseconds sampleInterval_;
    int radius_;
    std::shared_ptr<Shared> shared_;
    Telemetry telemetry_{};

    State state_{State::Idle};
    POINT lastPoint_{};
    bool haveLast_{false};
    std::chrono::steady_clock::time_point stableSince_{};
};
