// Deliberately hung UIA provider — test target for the deadline fix.
//
// Creates a normal visible window, then blocks its message loop forever inside
// WM_ENTERIDLE-free sleep. A window whose owning thread never pumps messages is
// exactly what makes UI Automation hang: UIA's cross-process calls marshal into
// the target's message queue, so a non-pumping thread stalls the caller for the
// UIA default timeout (~2 minutes) unless the caller bounds it.
//
// Usage:
//   hung_window.exe          -> window hangs 5s after it appears (default)
//   hung_window.exe 0        -> hangs immediately
//   hung_window.exe 15       -> hangs 15s after it appears
//
// The window title updates to show its state. Close it via Task Manager once
// hung (it cannot respond to a close request — that is the point).

#include <windows.h>

#include <cstdlib>
#include <cstdio>
#include <cwchar>

namespace {

UINT_PTR kHangTimer = 1;
DWORD g_hangDelayMs = 5000;

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kHangTimer) {
                KillTimer(hwnd, kHangTimer);
                SetWindowTextW(hwnd, L"HUNG - thread is now blocked forever");
                // Force the title to actually paint before we stop pumping.
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
                UpdateWindow(hwnd);
                // Block the UI thread permanently. No message pumping from here
                // on, so any UIA request targeting this window must wait.
                for (;;) Sleep(60000);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(40, 20, 20));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(240, 200, 200));
            SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
            const wchar_t* msg1 =
                L"Hover the overlay over THIS window once the title says HUNG.";
            const wchar_t* msg2 =
                L"The overlay must stay responsive. Kill me via Task Manager.";
            RECT t = rc;
            t.left += 16;
            t.top += 24;
            DrawTextW(hdc, msg1, -1, &t, DT_LEFT);
            t.top += 24;
            DrawTextW(hdc, msg2, -1, &t, DT_LEFT);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && *cmdLine) {
        const long v = wcstol(cmdLine, nullptr, 10);
        if (v >= 0) g_hangDelayMs = static_cast<DWORD>(v) * 1000;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HungWindowTestTarget";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) return 1;

    wchar_t title[128];
    _snwprintf(title, 128, L"Hang test target - will hang in %lu s",
               g_hangDelayMs / 1000);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title,
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 640, 200, nullptr, nullptr,
                                hInstance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, kHangTimer, g_hangDelayMs ? g_hangDelayMs : 1, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
