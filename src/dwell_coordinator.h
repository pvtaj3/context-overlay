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
//
// Threading contract
// ------------------
// sample()/cancel() are called on the UI (message) thread only. The STA worker
// NEVER invokes the dwell/cancel callbacks itself — window presentation is not
// thread safe, and UpdateLayeredWindow/SetWindowPos must run on the thread that
// owns the window. Instead the worker packages its result and PostMessage()s it
// to the coordinator's UI window; the message pump hands it to
// DwellCoordinator::deliverPosted(), which runs on the UI thread and is the only
// place onDwell is called from. onCancel is likewise only raised from
// sample()/cancel(), i.e. on the UI thread.
class DwellCoordinator {
public:
    // Private window message used to marshal a resolved identity back to the UI
    // thread. Registered against the coordinator's UI window; WPARAM is unused,
    // LPARAM owns a heap payload consumed by deliverPosted().
    static constexpr UINT kDwellResultMessage = WM_APP + 16;

    // `onDwell` receives the resolved target and the generation it belongs to.
    // It is always invoked on the UI thread.
    // `onCancel` fires when a pending/fired hover is broken by movement/hide.
    using DwellCallback = std::function<void(const HoverTarget&, uint64_t)>;
    using CancelCallback = std::function<void()>;

    // `uiWindow` must be a window owned by the calling (UI) thread; resolved
    // identities are posted to it. Construct after the window exists.
    DwellCoordinator(HWND uiWindow,
                     std::chrono::milliseconds dwell,
                     std::chrono::milliseconds sampleInterval,
                     int stabilityRadius,
                     DwellCallback onDwell,
                     CancelCallback onCancel)
        : dwell_(dwell),
          sampleInterval_(sampleInterval),
          radius_(stabilityRadius) {
        shared_ = std::make_shared<Shared>();
        shared_->uiWindow = uiWindow;
        shared_->onDwell = std::move(onDwell);
        shared_->onCancel = std::move(onCancel);
    }

    ~DwellCoordinator() {
        if (shared_) shared_->alive = false;  // detached workers exit on check
    }

    // Feed the current cursor position on every timer tick. UI thread only.
    void sample(POINT point);

    // Force-cancel any pending/fired dwell (e.g. on explicit hide). UI thread.
    void cancel();

    // Consume a kDwellResultMessage posted by a worker. Must be called from the
    // UI thread's message pump; takes ownership of the payload in `lp` and, if
    // the result is still current, invokes onDwell. Returns true if a target was
    // presented.
    static bool deliverPosted(WPARAM wp, LPARAM lp);

    // Telemetry (Phase Two: request cancellation + counts).
    uint64_t dwellCount() const { return shared_->telemetry.dwellFired.load(); }
    uint64_t identityFailCount() const {
        return shared_->telemetry.identityFail.load();
    }
    uint64_t cancelCount() const { return shared_->telemetry.canceled.load(); }

private:
    bool isStable(POINT a, POINT b) const;

    enum class State { Idle, Pending, Fired };

public:
    struct Telemetry {
        std::atomic<uint64_t> dwellFired{0};
        std::atomic<uint64_t> identityFail{0};
        std::atomic<uint64_t> canceled{0};
    };

    // Shared state handed to detached worker threads. Public so the free
    // resolveIdentityAndReport() helper can receive it by shared_ptr without
    // friending; it carries only atomics, the UI window handle, and the
    // callbacks (which the worker never calls — it only posts).
    struct Shared {
        std::atomic<uint64_t> generation{0};
        std::atomic<bool> alive{true};
        HWND uiWindow{};
        DwellCallback onDwell;
        CancelCallback onCancel;
        Telemetry telemetry;
    };

    // Heap payload marshalled through kDwellResultMessage. Keeping a strong
    // reference to Shared means the payload stays valid even if the coordinator
    // is destroyed while the message is still queued.
    struct PostedResult {
        std::shared_ptr<Shared> shared;
        HoverTarget target{};
        uint64_t generation{0};
    };

private:
    std::chrono::milliseconds dwell_;
    std::chrono::milliseconds sampleInterval_;
    int radius_;
    std::shared_ptr<Shared> shared_;

    State state_{State::Idle};
    POINT lastPoint_{};
    bool haveLast_{false};
    std::chrono::steady_clock::time_point stableSince_{};
};
