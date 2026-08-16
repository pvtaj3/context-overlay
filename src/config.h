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

}  // namespace config
