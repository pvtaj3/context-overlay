#include "overlay_window.h"
#include "config.h"
#include "diag.h"
#include "theme_tokens.h"

#include <algorithm>
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

// --- Windows 11 / WinUI DWM glue (issue #2) ----------------------------------
//
// These attributes and color values are resolved dynamically so the binary
// builds against older SDKs and still lights up the native styling on Windows
// 11. On systems that lack them (Win10, disabled composition) we take the
// documented graceful fallbacks and rely on the GDI card's own palette.

// DWM window attributes for system backdrop material + rounded corners.
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// DWM_SYSTEMBACKDROP_TYPE values (subset we use). Transient/overlay UI (Flyout,
// TeachingTip, MenuFlyout, tooltips) is documented to use the Acrylic
// (TRANSIENTWINDOW) material; Mica (MAINWINDOW) is for long-lived app windows.
// This card is a transient hover surface, so we default to Acrylic.
enum class DwmBackdrop : DWORD {
    kNone = 1,      // DWMSBT_NONE
    kMica = 2,      // DWMSBT_MAINWINDOW
    kAcrylic = 3,   // DWMSBT_TRANSIENTWINDOW
};

enum class DwmCorner : DWORD {
    kDefault = 0,
    kRound = 2,
    kRoundSmall = 3,
};

using DwmSetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

DwmSetAttrFn getDwmSetWindowAttribute() {
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) return nullptr;
    return reinterpret_cast<DwmSetAttrFn>(reinterpret_cast<void*>(
        GetProcAddress(dwm, "DwmSetWindowAttribute")));
}

// Apply the Win11-native look: rounded corners, immersive dark mode to match
// the chosen theme, and a system backdrop material (mica/acrylic) when the
// platform supports it. Every call is guarded by a runtime capability check so
// an absent API degrades to the GDI card's opaque fallback without error.
void applyWin11Backdrop(HWND hwnd, theme::ThemeMode mode,
                        theme::Backdrop backdrop) {
    auto setAttr = getDwmSetWindowAttribute();
    if (!setAttr) {
        diag::logf(L"win11: DwmSetWindowAttribute unavailable; GDI fallback "
                    L"(square corners, opaque card)");
        return;  // older OS: GDI palette + square corners are fine
    }

    // Rounded corners — the heart of the Win11 silhouette.
    const DwmCorner cornerPref = DwmCorner::kRound;
    setAttr(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref,
            sizeof(cornerPref));

    // Immersive dark mode so the non-client/backdrop tints follow our theme.
    const BOOL immersive = (mode == theme::ThemeMode::kDark) ? TRUE : FALSE;
    setAttr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));

    // System backdrop material. This card is transient overlay UI, so Acrylic
    // (DWMSBT_TRANSIENTWINDOW) is the documented material — it matches native
    // Flyout/TeachingTip surfaces. We request it via the SYSTEMBACKDROP_TYPE
    // attribute; on systems without it (Win10 / composition disabled) the call is
    // a no-op and we keep the GDI card's own translucent surface.
    DwmBackdrop dwmBackdrop = DwmBackdrop::kAcrylic;
    switch (backdrop) {
        case theme::Backdrop::kMica:
            dwmBackdrop = DwmBackdrop::kMica;
            break;
        case theme::Backdrop::kAcrylic:
            dwmBackdrop = DwmBackdrop::kAcrylic;
            break;
        case theme::Backdrop::kNone:
        default:
            dwmBackdrop = DwmBackdrop::kNone;
            break;
    }
    setAttr(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &dwmBackdrop, sizeof(dwmBackdrop));

    const wchar_t* bd = backdrop == theme::Backdrop::kMica    ? L"mica"
                       : backdrop == theme::Backdrop::kAcrylic ? L"acrylic"
                                                               : L"none";
    diag::logf(L"win11: corner=round immersive=%d backdrop=%s",
               immersive ? 1 : 0, bd);
}

// Per-monitor DPI for the overlay's own window (we are PMv2-aware). Falls back
// to system DPI when the per-window API is unavailable, so corner radii and
// typography scale correctly regardless of which monitor the card lands on.
UINT currentDpi(HWND hwnd) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto f = reinterpret_cast<GetDpiFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
        if (f) return f(hwnd);
    }
    return GetDpiForSystem();
}

// COLORREF is BGR; our token Rgb is RGB. Convert so layout assumptions never
// leak from the platform-neutral header into the renderer.
constexpr COLORREF toColorref(theme::Rgb c) {
    return RGB(c.r, c.g, c.b);
}

}  // namespace

bool OverlayWindow::create(HINSTANCE instance) {
    enableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ContextOverlayWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
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
    bi.bmiHeader.biBitCount = 32;     // BGRA: per-pixel alpha for corner masking
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    width_ = width;
    height_ = height;
}

// Paint a themed, rounded, per-pixel-alpha card into the layered DIB. The DIB
// starts fully transparent; we fill only inside the rounded rectangle, so the
// composited surface is genuinely masked — no square corners survive.
HDC OverlayWindow::beginCard(int width, int height,
                             const theme::ThemeTokens& tokens, int radius,
                             HBITMAP& outOld) {
    ensureDib(width, height);

    // The DIB is created transparent (CreateDIBSection zeroes it), so outside
    // the rounded rect stays alpha 0.

    HDC memdc = CreateCompatibleDC(nullptr);
    outOld = reinterpret_cast<HBITMAP>(SelectObject(memdc, dib_));

    // Rounded-rect clip so text/border never spill past the masked corners.
    HRGN clip = CreateRoundRectRgn(0, 0, width, height, radius * 2, radius * 2);
    SelectClipRgn(memdc, clip);
    DeleteObject(clip);

    // Card surface — a translucent tint so the DWM acrylic backdrop reads
    // through (native transient UI is not an opaque rectangle). Text contrast is
    // preserved because the tint is near-opaque; the material shows at the edges
    // and in any non-text area.
    HBRUSH bg = CreateSolidBrush(toColorref(tokens.cardBg));
    RECT rc{0, 0, width, height};
    FillRect(memdc, &rc, bg);
    DeleteObject(bg);

    // Hairline border: a 1px inner stroke in the neutral CardStroke tint
    // (WinUI Flyout/TeachingTip framing), NOT a saturated accent frame. Drawn
    // inset by 1px so it survives the rounded-corner clip. Use a thin pen
    // rather than FrameRect so we control the exact 1px weight and color.
    HPEN borderPen = CreatePen(PS_SOLID, 1, toColorref(tokens.cardStroke));
    HGDIOBJ oldPen = SelectObject(memdc, borderPen);
    // Draw a rectangle one pixel inside the clip region (which is already
    // rounded), so only the hairline shows, not a filled frame.
    Rectangle(memdc, 1, 1, width - 1, height - 1);
    SelectObject(memdc, oldPen);
    DeleteObject(borderPen);

    // System UI font. Win11's real UI face is "Segoe UI Variable"; fall back to
    // "Segoe UI" (and then the message font) if absent. Sized for the card and
    // DPI. Replaced each present because the chosen size is per-card.
    if (font_) DeleteObject(font_);
    LOGFONTW lf{};
    lf.lfHeight = -16;  // ~12pt at 96 DPI; GDI scales by the DC's DPI
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;  // matches Win11 text rendering
    lf.lfCharSet = DEFAULT_CHARSET;
    // Prefer the real Win11 face; older systems ignore the unknown name and
    // keep the empty lfFaceName (which maps to the system default UI font).
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable");
    font_ = CreateFontIndirectW(&lf);
    if (!font_) {
        LOGFONTW lf2 = lf;
        wcscpy_s(lf2.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf2);
    }
    SelectObject(memdc, font_);
    SetBkMode(memdc, TRANSPARENT);
    SetTextColor(memdc, toColorref(tokens.cardText));

    return memdc;
}

// Live OS dark-mode preference, read from the Personalize registry. Falls back
// to the app default if the key is absent, so absence never breaks the overlay.
bool OverlayWindow::systemPrefersDark() {
    DWORD value = 0, size = sizeof(value);
    constexpr wchar_t kPath[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    const LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, kPath,
                                   L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                                   nullptr, &value, &size);
    if (s != ERROR_SUCCESS) return theme::defaultMode() == theme::ThemeMode::kDark;
    // AppsUseLightTheme=0 -> dark apps. Our card follows the *app* switch.
    return value == 0;
}

void OverlayWindow::replay() {
    if (!visible_) return;
    if (lastIsIdentity_)
        showIdentity(lastTarget_);
    else
        showCardAt(lastAnchor_);
}

// Force the DIB alpha channel: opaque inside the rounded rect, transparent in
// the corner circles. GDI fill/text leave alpha at 0, so without this the
// entire card would composite as invisible under AC_SRC_ALPHA.
void OverlayWindow::maskRoundedCorners(int width, int height, int radius,
                                       unsigned char insideAlpha) {
    if (!dibBits_) return;
    auto* px = static_cast<unsigned char*>(dibBits_);  // BGRA, top-down
    const int r = std::max(radius, 0);

    // Default: every pixel opaque at `insideAlpha`.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            px[(y * width + x) * 4 + 3] = insideAlpha;
        }
    }
    if (r <= 0) return;  // square card, no corner punch-out needed

    // Punch transparent holes in the four corners (Euclidean inside the circle).
    const int r2 = r * r;
    auto insideCorner = [&](int cx, int cy, int x, int y) {
        const int dx = x - cx, dy = y - cy;
        return (dx * dx + dy * dy) > r2;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool transparent = false;
            if (x < r && y < r)
                transparent = insideCorner(r, r, x, y);          // top-left
            else if (x >= width - r && y < r)
                transparent = insideCorner(width - r, r, x, y);   // top-right
            else if (x < r && y >= height - r)
                transparent = insideCorner(r, height - r, x, y);  // bottom-left
            else if (x >= width - r && y >= height - r)
                transparent = insideCorner(width - r, height - r, x, y);  // BR
            if (transparent) px[(y * width + x) * 4 + 3] = 0;
        }
    }
}

// Composite the painted DIB as a layered, click-through, topmost surface and
// apply the Win11 DWM attributes. The DIB must remain selected into memdc until
// after UpdateLayeredWindow (the original Phase 1 bug: deselecting early
// composited nothing).
void OverlayWindow::endCard(HDC memdc, HBITMAP old, int width, int height,
                            int x, int y, theme::ThemeMode mode,
                            theme::Backdrop backdrop, int radius,
                            unsigned char surfaceAlpha) {
    // Mask the rounded corners into the alpha channel before compositing.
    maskRoundedCorners(width, height, radius, surfaceAlpha);

    POINT ptSrc{0, 0};
    POINT ptPos{x, y};
    SIZE size{width, height};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;   // full; per-pixel alpha carries the shape
    bf.AlphaFormat = AC_SRC_ALPHA;  // honor the DIB's alpha channel (corners)

    // Apply the native Win11 framing to the window *before* it is shown so the
    // first present already has rounded corners + immersive dark mode.
    applyWin11Backdrop(hwnd_, mode, backdrop);

    diag::logf(L"present: mode=%s dpi=%u radius=%d alpha=%u size=%dx%d "
                L"pos=(%d,%d)",
               mode == theme::ThemeMode::kDark ? L"dark" : L"light",
               static_cast<unsigned>(currentDpi(hwnd_)), radius, surfaceAlpha,
               width, height, x, y);

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const BOOL ok = UpdateLayeredWindow(hwnd_, nullptr, &ptPos, &size, memdc,
                                        &ptSrc, RGB(0, 0, 0), &bf, ULW_ALPHA);
    if (!ok) diag::logf(L"UpdateLayeredWindow FAILED err=%lu", GetLastError());

    SelectObject(memdc, old);  // restore only after compositing
    DeleteDC(memdc);
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

    // Theme tokens + scale-aware geometry (issue #2). Follow the OS preference
    // for now; the runtime preference-change path hooks WM_SETTINGCHANGE.
    UINT dpi = currentDpi(hwnd_);
    const int radius = theme::cornerRadiusForDpi(static_cast<int>(dpi));
    const theme::ThemeMode mode = theme::selectMode(true, systemPrefersDark());
    const theme::ThemeTokens tokens = theme::resolveTheme(mode);
    // Backdrop capability: assume supported on this build; the DWM call itself
    // is a no-op on platforms lacking the API. Acrylic (transient-UI material)
    // matches native Flyout/TeachingTip surfaces.
    const theme::Backdrop backdrop =
        theme::effectiveBackdrop(theme::Backdrop::kAcrylic, true);
    // Slightly translucent so the acrylic material reads through without harming
    // the WCAG-contrast text.
    const unsigned char surfaceAlpha =
        (backdrop == theme::Backdrop::kNone) ? 255 : 210;

    HBITMAP old{};
    HDC memdc = beginCard(w, h, tokens, radius, old);

    wchar_t line1[64];
    wsprintfW(line1, L"Context Overlay  -  Phase 1");
    wchar_t line2[64];
    wsprintfW(line2, L"Dwell @ (%ld, %ld)", anchor.x, anchor.y);

    RECT textRc{0, 0, w, h};
    textRc.left += 12;
    textRc.top += 12;
    DrawTextW(memdc, line1, -1, &textRc, DT_LEFT);
    textRc.top += 22;
    DrawTextW(memdc, line2, -1, &textRc, DT_LEFT);

    lastAnchor_ = anchor;
    lastIsIdentity_ = false;
    visible_ = true;
    endCard(memdc, old, w, h, x, y, mode, backdrop, radius, surfaceAlpha);
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

    UINT dpi = currentDpi(hwnd_);
    const int radius = theme::cornerRadiusForDpi(static_cast<int>(dpi));
    const theme::ThemeMode mode = theme::selectMode(true, systemPrefersDark());
    const theme::ThemeTokens tokens = theme::resolveTheme(mode);
    const theme::Backdrop backdrop =
        theme::effectiveBackdrop(theme::Backdrop::kAcrylic, true);
    const unsigned char surfaceAlpha =
        (backdrop == theme::Backdrop::kNone) ? 255 : 210;

    HBITMAP old{};
    HDC memdc = beginCard(w, h, tokens, radius, old);

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

    lastTarget_ = target;
    lastIsIdentity_ = true;
    visible_ = true;
    endCard(memdc, old, w, h, x, y, mode, backdrop, radius, surfaceAlpha);
}

void OverlayWindow::hide() {
    visible_ = false;
    ShowWindow(hwnd_, SW_HIDE);
}

OverlayWindow::~OverlayWindow() {
    if (dib_) DeleteObject(dib_);
    if (font_) DeleteObject(font_);
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

        case WM_SETTINGCHANGE:
            // System theme / color preference changed: re-present the visible
            // card under the new theme without waiting for the next dwell.
            diag::logf(L"WM_SETTINGCHANGE received; re-presenting under new theme");
            replay();
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
