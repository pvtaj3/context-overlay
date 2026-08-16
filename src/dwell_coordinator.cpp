#include "dwell_coordinator.h"

#include <windows.h>

void DwellCoordinator::sample(POINT point) {
    const auto now = std::chrono::steady_clock::now();

    // Any movement beyond the stability radius breaks the current hover.
    if (!haveLast_ || !isStable(point, lastPoint_)) {
        const bool wasActive =
            (state_ == State::Pending || state_ == State::Fired);
        haveLast_ = true;
        lastPoint_ = point;
        state_ = State::Idle;
        if (wasActive && onCancel_) onCancel_();
        return;
    }

    lastPoint_ = point;

    switch (state_) {
        case State::Idle:
            // First stable sample: start the dwell clock.
            state_ = State::Pending;
            stableSince_ = now;
            break;

        case State::Pending:
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stableSince_) >= dwell_) {
                state_ = State::Fired;
                ++generation_;
                if (onDwell_) onDwell_(point);
            }
            break;

        case State::Fired:
            // Stay fired until the cursor moves; do not re-trigger while resting.
            break;
    }
}

void DwellCoordinator::cancel() {
    if (state_ != State::Idle && onCancel_) onCancel_();
    state_ = State::Idle;
    haveLast_ = false;
}

bool DwellCoordinator::isStable(POINT a, POINT b) const {
    return std::abs(a.x - b.x) <= radius_ && std::abs(a.y - b.y) <= radius_;
}
