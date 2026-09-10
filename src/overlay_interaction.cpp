#include "overlay_interaction.h"

namespace overlay_interaction {

namespace {
constexpr UINT kModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
}

bool registerToggleHotkey(HWND hwnd) {
    return RegisterHotKey(hwnd, kToggleHotkeyId, kModifiers, VK_SPACE) != FALSE;
}

void unregisterToggleHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, kToggleHotkeyId);
}

bool isToggleHotkey(WPARAM id) {
    return id == static_cast<WPARAM>(kToggleHotkeyId);
}

void animateVisibility(HWND hwnd, bool visible) {
    // SWP_NOACTIVATE is essential: this is a click-through, non-activating HUD.
    // Native DWM/WinUI composition can animate the resulting surface; systems
    // without composition still receive a correct, immediate fallback.
    ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                 (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void toggleVisible(HWND hwnd) {
    animateVisibility(hwnd, IsWindowVisible(hwnd) == FALSE);
}

}  // namespace overlay_interaction
