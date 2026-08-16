#include <windows.h>

#include <objbase.h>

#include "config.h"
#include "dwell_coordinator.h"
#include "overlay_window.h"
#include "types.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Initialize COM on the UI thread per the architecture's startup sequence.
    // UI Automation itself runs on a dedicated STA worker (see the coordinator),
    // but initializing here keeps the process apartment model well-defined.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    OverlayWindow overlay;
    if (!overlay.create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create the overlay window.",
                    L"Context Overlay", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Phase Two: dwell now resolves a HoverTarget (anchor + HWND + stable
    // element hash) off-thread and reports it with its generation. Movement or
    // an explicit cancel bumps the generation, so any slow probe from a previous
    // hover is discarded.
    DwellCoordinator coordinator(
        config::kDwellThreshold,
        config::kSampleInterval,
        config::kStabilityRadiusPx,
        [&](const HoverTarget& target, uint64_t generation) {
            (void)generation;  // arbitration lives in the pipeline (Phase 3+);
                                // here we simply render the resolved identity.
            overlay.showIdentity(target);
        },
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

    CoUninitialize();
    return 0;
}
