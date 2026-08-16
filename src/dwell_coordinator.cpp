#include "dwell_coordinator.h"

#include <windows.h>

#include <objbase.h>

#include <memory>
#include <thread>
#include <utility>

#include "config.h"
#include "uia_identity.h"

namespace {

// Resolve the hover identity off the UI thread, in a fresh STA apartment, then
// marshal the result back to the UI thread via PostMessage. Mirrors the
// architecture's threading model: the UI/message thread never calls UI
// Automation, and — just as importantly — this worker never touches the overlay
// window. Presentation happens only on the thread that owns the window.
void resolveIdentityAndReport(std::shared_ptr<DwellCoordinator::Shared> shared,
                              POINT point, uint64_t generation,
                              std::chrono::steady_clock::time_point stableSince) {
    if (!shared) return;

    // Own STA for the lifetime of this probe. Only call CoUninitialize when we
    // actually initialized: S_OK and S_FALSE both mean "initialized, balance
    // it"; RPC_E_CHANGED_MODE means another apartment model already owns this
    // thread and we must NOT uninitialize it; any other failure means COM is
    // unusable here.
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needsUninit = (coHr == S_OK || coHr == S_FALSE);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        // COM could not be initialized at all: this dwell cannot be resolved.
        ++shared->telemetry.identityFail;
        return;
    }

    auto identity = identifyAt(point, config::kUiaDeadlineMs);

    if (!identity) {
        // Resolution genuinely failed (no UIA, protected surface, deadline hit
        // with nothing readable). Count it exactly once, whether or not the
        // hover is still current — the failure happened either way.
        ++shared->telemetry.identityFail;
    } else {
        // Arbitration: if the hover was superseded or the coordinator is gone,
        // drop the result. UIA objects are released inside identifyAt.
        const bool stillValid =
            shared->alive.load() && shared->generation.load() == generation;
        if (stillValid && shared->uiWindow) {
            auto payload = std::make_unique<DwellCoordinator::PostedResult>();
            payload->shared = shared;
            payload->target.screenPoint = point;
            payload->target.hwnd = identity->hwnd;
            payload->target.elementHash = identity->elementHash;
            payload->target.stableSince = stableSince;
            payload->generation = generation;

            // Hand ownership to the message queue. If the post fails (window
            // already destroyed, queue full) we must free it ourselves.
            if (PostMessageW(shared->uiWindow,
                             DwellCoordinator::kDwellResultMessage, 0,
                             reinterpret_cast<LPARAM>(payload.get()))) {
                payload.release();
            }
        }
    }

    if (needsUninit) CoUninitialize();
}

}  // namespace

bool DwellCoordinator::deliverPosted(WPARAM /*wp*/, LPARAM lp) {
    // Runs on the UI thread; takes ownership of the payload unconditionally.
    std::unique_ptr<PostedResult> payload(reinterpret_cast<PostedResult*>(lp));
    if (!payload || !payload->shared) return false;

    auto& shared = *payload->shared;
    // Re-check arbitration: the cursor may have moved between the post and the
    // pump picking it up.
    if (!shared.alive.load()) return false;
    if (shared.generation.load() != payload->generation) return false;
    if (!shared.onDwell) return false;

    shared.onDwell(payload->target, payload->generation);
    return true;
}

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
            ++shared->telemetry.canceled;
            if (shared->onCancel) shared->onCancel();  // UI thread: safe
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
                ++shared->telemetry.dwellFired;
                const uint64_t gen = shared->generation.load();
                const auto since = stableSince_;
                // Detached worker: keeps `shared` alive, never blocks the UI
                // thread (no join), exits cleanly if alive flips to false, and
                // reports back only by PostMessage.
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
        ++shared->telemetry.canceled;
        if (shared->onCancel) shared->onCancel();  // UI thread: safe
    }
    state_ = State::Idle;
    haveLast_ = false;
}

bool DwellCoordinator::isStable(POINT a, POINT b) const {
    return std::abs(a.x - b.x) <= radius_ && std::abs(a.y - b.y) <= radius_;
}
