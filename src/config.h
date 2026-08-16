#pragma once

#include <chrono>

// Phase One tuning constants. Later these will be loaded from a config file or
// the registry; for now they are compile-time so the behaviour is easy to read.
namespace config {

// How long the cursor must remain stable before a dwell is reported.
inline constexpr std::chrono::milliseconds kDwellThreshold{400};

// Polling cadence for cursor sampling (GetCursorPos). The architecture document
// recommends 40-60 ms; this keeps the UI thread cheap while staying responsive.
inline constexpr std::chrono::milliseconds kSampleInterval{50};

// The cursor must stay within this radius (device pixels, pre-DPI-scaling) to
// count as "not moving". Small enough to ignore jitter, large enough to ignore
// micro-drift.
inline constexpr int kStabilityRadiusPx{4};

// Test-card geometry. Phase One only; replaced by the Direct2D renderer later.
inline constexpr int kCardWidth{300};
inline constexpr int kCardHeight{84};

// Overall opacity of the test card, 0 (invisible) .. 255 (opaque). Applied as a
// single global alpha multiply; the card has no per-pixel alpha yet.
inline constexpr BYTE kCardAlpha{200};

// Phase Two: wall-clock deadline for the UI Automation identity probe. UI
// Automation cancellation is not universal across providers, so we abandon stale
// results after this budget rather than block on them.
inline constexpr DWORD kUiaDeadlineMs{400};

// Phase Two: how long the card stays shown before re-probing identity when the
// cursor rests on a *new* element. Kept short; the dwell coordinator already
// arbitrates via its generation counter.
inline constexpr std::chrono::milliseconds kIdentityRefresh{800};

}  // namespace config
