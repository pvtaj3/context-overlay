#include "overlay_window.h"
#include "config.h"
#include "diag.h"

#include <cstdio>
#include <cwchar>

#include <windows.h>

namespace {
constexpr UINT WM_SHOW_CARD = WM_APP + 1;
constexpr UINT WM_HIDE_CARD = WM_APP + 2;

// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 as a HANDLE constant, for the
// toolchains whose headers predate the macro.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

// Robust, toolchain-agnostic DPI awareness: prefer per-monitor v2, fall back to
// the universal SetProcessDPIAware. Done dynamically so the same sources compile
// on MSVC and MinGW without SDK-version surprises.
void enableDpiAwareness() {
    using SetDpiCtxFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto f = reinterpret_cast<SetDpiCtxFn>(reinterpret_cast<void*>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (f && f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}
}  // namespace

bool OverlayWindow::create(HINSTANCE instance) {
    enableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ContextOverlayWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    // No cursor: we are a transparent overlay and must not advertise
    // interactivity to the user or the shell.
    wc.hCursor = nullptr;
    if (!RegisterClassExW(&wc)) return false;

    // WS_EX_LAYERED      -> per-pixel/alpha compositing via UpdateLayeredWindow
    // WS_EX_TRANSPARENT   -> clicks fall through to the window beneath
    // WS_EX_NOACTIVATE    -> never becomes the foreground/active window
    // WS_EX_TOPMOST       -> always above the workspace
    // WS_EX_TOOLWINDOW    -> hidden from Alt-Tab, no taskbar entry
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    // Park it off-screen and hidden until the first dwell.
    SetWindowPos(hwnd_, HWND_TOPMOST, -32000, -32000, 1, 1,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_HIDEWINDOW);
    return true;
}

void OverlayWindow::ensureDib(int width, int height) {
    if (dib_ && width_ == width && height_ == height) return;
    if (dib_) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    width_ = width;
    height_ = height;
}

void OverlayWindow::paintCard(HDC hdc, int width, int height, POINT anchor) {
    RECT rc{0, 0, width, height};

    // Card background.
    HBRUSH bg = CreateSolidBrush(RGB(26, 27, 33));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // Accent border.
    HBRUSH border = CreateSolidBrush(RGB(96, 165, 250));
    FrameRect(hdc, &rc, border);
    DeleteObject(border);

    // Text.
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(232, 234, 238));
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(hdc, font);

    wchar_t line1[64];
    wsprintfW(line1, L"Context Overlay  -  Phase 1");
    wchar_t line2[64];
    wsprintfW(line2, L"Dwell @ (%ld, %ld)", anchor.x, anchor.y);

    RECT textRc = rc;
    textRc.left += 12;
    textRc.top += 12;
    DrawTextW(hdc, line1, -1, &textRc, DT_LEFT);
    textRc.top += 22;
    DrawTextW(hdc, line2, -1, &textRc, DT_LEFT);
}

void OverlayWindow::showCardAt(POINT anchor) {
    const int w = config::kCardWidth;
    const int h = config::kCardHeight;

    // Place the card near the cursor, flipping/clamping to the work area so it
    // never runs off-screen.
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = anchor.x + 16;
    int y = anchor.y + 16;
    if (x + w > wa.right) x = anchor.x - w - 16;
    if (y + h > wa.bottom) y = anchor.y - h - 16;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    ensureDib(w, h);

    HDC memdc = CreateCompatibleDC(nullptr);
    HBITMAP old = reinterpret_cast<HBITMAP>(SelectObject(memdc, dib_));
    paintCard(memdc, w, h, anchor);

    // Composite the DIB as a layered surface. AlphaFormat = 0 means we use a
    // single global alpha (kCardAlpha); per-pixel alpha arrives with D2D later.
    POINT ptSrc{0, 0};
    POINT ptPos{x, y};
    SIZE size{w, h};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = config::kCardAlpha;
    bf.AlphaFormat = 0;

    // UpdateLayeredWindow reads the bitmap OUT OF the source DC, so the DIB must
    // still be selected here. Deselecting before this call (the original bug)
    // makes it read the DC's default 1x1 monochrome bitmap and composite
    // nothing, leaving the overlay permanently invisible.
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const BOOL ok = UpdateLayeredWindow(hwnd_, nullptr, &ptPos, &size, memdc,
                                        &ptSrc, RGB(0, 0, 0), &bf, ULW_ALPHA);
    if (!ok) diag::logf(L"UpdateLayeredWindow FAILED err=%lu", GetLastError());

    SelectObject(memdc, old);  // restore only after compositing
    DeleteDC(memdc);
}

void OverlayWindow::showIdentity(const HoverTarget& target) {
    // Phase Two: a wider card that surfaces the resolved identity so the
    // dwell/arbitration behaviour is observable. The real D2D card (Phase 5+)
    // will replace this.
    const int w = config::kCardWidth + 60;
    const int h = config::kCardHeight + 46;  // room for the telemetry line

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = target.screenPoint.x + 16;
    int y = target.screenPoint.y + 16;
    if (x + w > wa.right) x = target.screenPoint.x - w - 16;
    if (y + h > wa.bottom) y = target.screenPoint.y - h - 16;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    ensureDib(w, h);

    HDC memdc = CreateCompatibleDC(nullptr);
    HBITMAP old = reinterpret_cast<HBITMAP>(SelectObject(memdc, dib_));
    paintCard(memdc, w, h, target.screenPoint);

    // Identity text lines (drawn over the base card background).
    SetBkMode(memdc, TRANSPARENT);
    SetTextColor(memdc, RGB(232, 234, 238));
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(memdc, font);

    wchar_t l1[96];
    wsprintfW(l1, L"Phase 2  -  identity");
    wchar_t l2[96];
    // _snwprintf, not wsprintfW: the Win32 formatter does not support %016llX,
    // so the element hash previously rendered as garbage.
    _snwprintf(l2, 96, L"hwnd=0x%p  hash=%016llX",
               static_cast<void*>(target.hwnd),
               static_cast<unsigned long long>(target.elementHash));

    RECT rc{0, 0, w, h};
    rc.left += 12;
    rc.top += 12;
    DrawTextW(memdc, l1, -1, &rc, DT_LEFT);
    rc.top += 22;
    DrawTextW(memdc, l2, -1, &rc, DT_LEFT);

    // Telemetry line: makes dwell / identity-fail / cancel counts observable at
    // runtime. Without this the identityFail fix cannot be confirmed by running
    // the app at all.
    if (counters_) {
        wchar_t l3[96];
        _snwprintf(l3, 96, L"dwell=%llu  fail=%llu  cancel=%llu",
                   static_cast<unsigned long long>(counters_->dwell),
                   static_cast<unsigned long long>(counters_->identityFail),
                   static_cast<unsigned long long>(counters_->cancel));
        rc.top += 22;
        DrawTextW(memdc, l3, -1, &rc, DT_LEFT);
    }

    POINT ptSrc{0, 0};
    POINT ptPos{x, y};
    SIZE size{w, h};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = config::kCardAlpha;
    bf.AlphaFormat = 0;

    // As in showCardAt: the DIB must remain selected into memdc across this call.
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const BOOL ok = UpdateLayeredWindow(hwnd_, nullptr, &ptPos, &size, memdc,
                                        &ptSrc, RGB(0, 0, 0), &bf, ULW_ALPHA);
    diag::logf(L"showIdentity: pos=(%d,%d) size=%dx%d ulw=%d err=%lu",
               x, y, w, h, ok ? 1 : 0, ok ? 0UL : GetLastError());

    SelectObject(memdc, old);  // restore only after compositing
    DeleteDC(memdc);
}

void OverlayWindow::hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

OverlayWindow::~OverlayWindow() {
    if (dib_) DeleteObject(dib_);
}

LRESULT OverlayWindow::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            // The single most important click-through guarantee: tell Windows
            // the cursor is "over" whatever window is beneath us.
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            // Never steal activation when the user clicks through us.
            return MA_NOACTIVATE;

        case WM_SHOW_CARD:
            showCardAt(*reinterpret_cast<POINT*>(&wp));
            return 0;

        case WM_HIDE_CARD:
            hide();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK OverlayWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp,
                                        LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
