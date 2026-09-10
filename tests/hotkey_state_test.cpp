#include <cstdio>

enum class HotkeyState { kUnregistered, kRegistered, kConflict };

static bool canToggle(HotkeyState state) { return state == HotkeyState::kRegistered; }

int main() {
    if (!canToggle(HotkeyState::kRegistered) ||
        canToggle(HotkeyState::kUnregistered) ||
        canToggle(HotkeyState::kConflict)) return 1;
    std::puts("hotkey state tests passed");
    return 0;
}
