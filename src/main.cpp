#include <windows.h>

#include <objbase.h>

#include "config.h"
#include "diag.h"
#include "dwell_coordinator.h"
#include "overlay_window.h"
#include "types.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Initialize COM on the UI thread per the architecture's startup sequence.
    // UI Automation itself runs on a dedicated STA worker (see the coordinator),
    // but initializing here keeps the process apartment model well-defined.
    //
    // S_OK and S_FALSE both mean "this thread is initialized, balance it with
    // CoUninitialize". RPC_E_CHANGED_MODE means the thread already belongs to a
    // different apartment model and we must NOT uninitialize it. Any other
    // failure is fatal for our purposes.
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = (coHr == S_OK || coHr == S_FALSE);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"Context Overlay",
                    MB_ICONERROR);
        return 1;
    }

    diag::logf(L"=== context_overlay start (coHr=0x%08lX) ===",
               static_cast<unsigned long>(coHr));

    OverlayWindow overlay;
    if (!overlay.create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create the overlay window.",
                    L"Context Overlay", MB_ICONERROR);
        if (comInitialized) CoUninitialize();
        return 1;
    }

    // Phase Two: dwell now resolves a HoverTarget (anchor + HWND + stable
    // element hash) off-thread and marshals it back here with its generation.
    // Movement or an explicit cancel bumps the generation, so any slow probe
    // from a previous hover is discarded. The overlay is only ever touched from
    // this (UI) thread: the STA worker posts kDwellResultMessage to the overlay
    // window and the pump below turns that into the onDwell call.
    diag::logf(L"overlay window created hwnd=0x%p",
               static_cast<void*>(overlay.hwnd()));

    // Telemetry snapshot rendered on the card. Refreshed on the UI thread right
    // before each present, so the counters shown are current.
    OverlayWindow::Counters counters{};
    overlay.setCounters(&counters);

    DwellCoordinator* coordinatorPtr = nullptr;
    DwellCoordinator coordinator(
        overlay.hwnd(),
        config::kDwellThreshold,
        config::kSampleInterval,
        config::kStabilityRadiusPx,
        [&](const HoverTarget& target, uint64_t generation) {
            (void)generation;  // arbitration already applied before delivery
            if (coordinatorPtr) {
                counters.dwell = coordinatorPtr->dwellCount();
                counters.identityFail = coordinatorPtr->identityFailCount();
                counters.cancel = coordinatorPtr->cancelCount();
            }
            overlay.showIdentity(target);
        },
        [&]() { overlay.hide(); });
    coordinatorPtr = &coordinator;

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
        // Identity resolved by an STA worker: presentation happens here, on the
        // thread that owns the overlay window.
        if (msg.message == DwellCoordinator::kDwellResultMessage) {
            DwellCoordinator::deliverPosted(msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    diag::logf(L"=== context_overlay exit (dwell=%llu fail=%llu cancel=%llu) ===",
               static_cast<unsigned long long>(coordinator.dwellCount()),
               static_cast<unsigned long long>(coordinator.identityFailCount()),
               static_cast<unsigned long long>(coordinator.cancelCount()));
    KillTimer(overlay.hwnd(), 1);
    if (comInitialized) CoUninitialize();
    return 0;
}
