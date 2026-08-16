#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

// Identity of a stable hover at a point in time. Phase One keeps only the
// anchor; the architecture's HoverTarget also tracks HWND + UIA element hash,
// which arrive with the text-grabber phase.
struct HoverTarget {
    POINT screenPoint{};
    std::chrono::steady_clock::time_point stableSince{};
};

// Detects a deliberate mouse "dwell": the cursor resting within a small radius
// for at least `dwell` milliseconds. It is designed to run entirely on the UI
// thread, driven by a periodic timer (the architecture document's recommended
// GetCursorPos-polling approach), so it never installs a hook and never touches
// native input throughput.
class DwellCoordinator {
public:
    using DwellCallback = std::function<void(POINT anchor)>;
    using CancelCallback = std::function<void()>;

    DwellCoordinator(std::chrono::milliseconds dwell,
                     std::chrono::milliseconds sampleInterval,
                     int stabilityRadius,
                     DwellCallback onDwell,
                     CancelCallback onCancel)
        : dwell_(dwell),
          sampleInterval_(sampleInterval),
          radius_(stabilityRadius),
          onDwell_(std::move(onDwell)),
          onCancel_(std::move(onCancel)) {}

    // Feed the current cursor position on every timer tick.
    void sample(POINT point);

    // Force-cancel any pending/fired dwell (e.g. on explicit hide).
    void cancel();

private:
    bool isStable(POINT a, POINT b) const;

    enum class State { Idle, Pending, Fired };

    std::chrono::milliseconds dwell_;
    std::chrono::milliseconds sampleInterval_;
    int radius_;
    DwellCallback onDwell_;
    CancelCallback onCancel_;

    // Bumped on each fired dwell; used later to arbitrate stale captures.
    std::atomic<uint64_t> generation_{0};
    State state_{State::Idle};
    POINT lastPoint_{};
    bool haveLast_{false};
    std::chrono::steady_clock::time_point stableSince_{};
};
