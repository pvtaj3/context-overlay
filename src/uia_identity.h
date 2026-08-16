#pragma once

#include <windows.h>

#include <optional>

#include "types.h"

// Resolve the stable identity of whatever UI element sits under `point`.
//
// Must be called from a thread that has initialized COM for an STA apartment
// (the caller is responsible for CoInitializeEx(..., COINIT_APARTMENTTHREADED)
// before calling, and for checking that it succeeded). The architecture
// requires UI Automation work to stay off the UI thread, and the provider
// objects are never retained: only plain data (HWND, a stable hash, a couple of
// strings) is returned.
//
// `deadlineMs` is a hard wall-clock budget for the whole probe and is actually
// enforced:
//   - IUIAutomation2's connection/transaction timeouts are set to it when the
//     running UIA build exposes that interface, so cross-process calls to a
//     hung provider fail instead of blocking forever;
//   - the remaining budget is re-checked between every provider call, and the
//     probe returns early (with whatever it has) once the budget is spent.
//
// Returns nullopt when the probe produced no usable identity at all:
//   - COM/UIA is unavailable,
//   - the target is elevated / on a protected surface (UIA access denied),
//   - there is no window under the point,
//   - the deadline expired before anything could be resolved.
// Callers treat nullopt as an identity-resolution failure for telemetry.
std::optional<HoverIdentity> identifyAt(POINT point, DWORD deadlineMs = 400);
