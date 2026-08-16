#pragma once

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <string>

// Identity of a stable hover at a point in time. This is the architecture's
// HoverTarget, extended with the resolved HWND and a stable per-element hash
// (Phase 2: stable-hover identity). The hash lets the pipeline dedupe/arbitrate
// captures for "the same element" across dwell cycles without retaining any UIA
// COM object.
struct HoverTarget {
    POINT screenPoint{};
    HWND hwnd{};
    uint64_t elementHash{};
    std::chrono::steady_clock::time_point stableSince{};
};

// What the UI Automation probe discovered under the cursor. Computed off the UI
// thread; only plain data is kept here (never a raw IUIAutomationElement*).
struct HoverIdentity {
    HWND hwnd{};
    uint64_t elementHash{};  // stable hash derived from the UIA runtime id
    std::wstring controlType;  // e.g. L"Edit", L"Text"
    std::wstring name;          // element name; may be empty
};

// Captured text for a hover (Phase 3 fills this; declared now for the pipeline).
struct TextCapture {
    uint64_t requestId{};
    std::wstring text;
    std::wstring controlType;
    HWND sourceWindow{};
};

// Result handed back to the UI thread for rendering (Phase 4 fills markdown).
struct ContextResult {
    uint64_t requestId{};
    std::wstring markdown;
    bool complete{false};
    std::wstring error;
};
