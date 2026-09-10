#ifdef _WIN32
#include "../src/overlay_interaction.h"
#include <windows.h>
#include <cstdio>
int main() {
    HWND hwnd = GetConsoleWindow();
    const bool registered = overlay_interaction::registerToggleHotkey(hwnd);
    if (!registered) { std::puts("hotkey unavailable/conflicted"); return 0; }
    if (!overlay_interaction::isToggleHotkey(overlay_interaction::kToggleHotkeyId)) return 1;
    overlay_interaction::unregisterToggleHotkey(hwnd);
    return 0;
}
#else
int main() { return 0; }
#endif
