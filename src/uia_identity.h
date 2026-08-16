#pragma once

#include <windows.h>

#include <optional>

#include "types.h"

// Resolve the stable identity of whatever UI element sits under `point`.
//
// Must be called from a thread that has initialized COM for an STA apartment
// (the caller is responsible for CoInitializeEx(..., COINIT_APARTMENTTHREADED)
// before calling). The architecture requires UI Automation work to stay off the
// UI thread, and the provider objects are never retained: only plain data
// (HWND, a stable hash, a couple of strings) is returned.
//
// Returns nullopt when:
//   - COM/UIA is unavailable,
//   - the target is elevated / on a protected surface (UIA access denied),
//   - the resolve does not finish within `deadlineMs` (caller enforces this by
//     abandoning the result; UIA cancellation is not universal across providers).
std::optional<HoverIdentity> identifyAt(POINT point, DWORD deadlineMs = 400);
