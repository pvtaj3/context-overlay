#include <windows.h>

#include "config.h"
#include "dwell_coordinator.h"
#include "overlay_window.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    OverlayWindow overlay;
    if (!overlay.create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create the overlay window.",
                    L"Context Overlay", MB_ICONERROR);
        return 1;
    }

    // Dwell fires -> show the test card; movement -> hide it.
    DwellCoordinator coordinator(
        config::kDwellThreshold,
        config::kSampleInterval,
        config::kStabilityRadiusPx,
        [&](POINT p) { overlay.showCardAt(p); },
        [&]() { overlay.hide(); });

    // Sample the cursor on a periodic timer. This is the architecture's
    // recommended approach: no global hook, no interference with native input.
    SetTimer(overlay.hwnd(), 1,
             static_cast<UINT>(config::kSampleInterval.count()), nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_TIMER && msg.wParam == 1) {
            POINT p;
            if (GetCursorPos(&p)) coordinator.sample(p);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
