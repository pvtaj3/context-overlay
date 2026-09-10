#pragma once

#include <windows.h>

namespace overlay_interaction {

// Registers Ctrl+Shift+Space without activating the overlay. The caller owns
// the message loop and should route WM_HOTKEY to toggleVisible().
bool registerToggleHotkey(HWND hwnd);
void unregisterToggleHotkey(HWND hwnd);
bool isToggleHotkey(WPARAM id);
void toggleVisible(HWND hwnd);

// Applies a compositor-friendly transition. The implementation uses the
// documented window animation path when available and falls back to a direct
// show/hide, preserving click-through and non-activation semantics.
void animateVisibility(HWND hwnd, bool visible);

constexpr int kToggleHotkeyId = 0xC071;

}  // namespace overlay_interaction
