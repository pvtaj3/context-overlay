#include "dwell_coordinator.h"

#include <windows.h>

#include <objbase.h>

#include <thread>
#include <utility>

#include "config.h"
#include "uia_identity.h"

namespace {

// Resolve the hover identity off the UI thread, in a fresh STA apartment, then
// hand a HoverTarget back through the dwell callback. Mirrors the architecture's
// threading model: the UI/message thread never calls UI Automation.
void resolveIdentityAndReport(std::shared_ptr<DwellCoordinator::Shared> shared,
                              POINT point, uint64_t generation,
                              std::chrono::steady_clock::time_point stableSince) {
    // Own STA for the lifetime of this probe.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        // Already initialized on this thread (shouldn't happen for a fresh
        // detached thread, but tolerate it).
    }

    auto identity = identifyAt(point, config::kUiaDeadlineMs);

    // Arbitration: if the hover was superseded or the coordinator is gone,
    // drop the result. UIA objects are released inside identifyAt.
    const bool stillValid =
        shared && shared->alive.load() && shared->generation.load() == generation;
    if (stillValid && identity && shared->onDwell) {
        HoverTarget target{};
        target.screenPoint = point;
        target.hwnd = identity->hwnd;
        target.elementHash = identity->elementHash;
        target.stableSince = stableSince;
        shared->onDwell(target, generation);
    }

    CoUninitialize();
}

}  // namespace

void DwellCoordinator::sample(POINT point) {
    const auto now = std::chrono::steady_clock::now();
    auto shared = shared_;

    // Any movement beyond the stability radius breaks the current hover.
    if (!haveLast_ || !isStable(point, lastPoint_)) {
        const bool wasActive =
            (state_ == State::Pending || state_ == State::Fired);
        haveLast_ = true;
        lastPoint_ = point;
        state_ = State::Idle;
        if (wasActive) {
            ++shared->generation;  // supersede any in-flight probe
            ++telemetry_.canceled;
            if (shared->onCancel) shared->onCancel();
        }
        return;
    }

    lastPoint_ = point;

    switch (state_) {
        case State::Idle:
            // Start of a new hover epoch: bump generation so any probe from a
            // previous hover is now stale.
            ++shared->generation;
            state_ = State::Pending;
            stableSince_ = now;
            break;

        case State::Pending:
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stableSince_) >= dwell_) {
                state_ = State::Fired;
                ++telemetry_.dwellFired;
                const uint64_t gen = shared->generation.load();
                const auto since = stableSince_;
                // Detached worker: keeps `shared` alive, never blocks the UI
                // thread (no join), and exits cleanly if alive flips to false.
                std::thread([shared, point, gen, since]() {
                    resolveIdentityAndReport(shared, point, gen, since);
                }).detach();
            }
            break;

        case State::Fired:
            // Stay fired until the cursor moves; do not re-trigger while resting.
            break;
    }
}

void DwellCoordinator::cancel() {
    auto shared = shared_;
    if (state_ != State::Idle) {
        ++shared->generation;
        ++telemetry_.canceled;
        if (shared->onCancel) shared->onCancel();
    }
    state_ = State::Idle;
    haveLast_ = false;
}

bool DwellCoordinator::isStable(POINT a, POINT b) const {
    return std::abs(a.x - b.x) <= radius_ && std::abs(a.y - b.y) <= radius_;
}
