#pragma once

#include <windows.h>

#include <cstdint>

#include "theme_tokens.h"
#include "types.h"

// A topmost, transparent, click-through layered window.
//
// Phase One responsibility: own the Win32 window, keep it non-activating and
// click-through, and paint a simple translucent test card on demand near the
// cursor. The real Direct2D/DirectWrite card renderer is a later phase; here we
// use a GDI DIB section + UpdateLayeredWindow with a single global alpha, which
// is the most robust, dependency-light way to stand up the layered shell.
class OverlayWindow {
public:
    // Snapshot of the dwell coordinator's telemetry, rendered on the identity
    // card so the counters are observable at runtime. The overlay only reads it.
    struct Counters {
        uint64_t dwell{};
        uint64_t identityFail{};
        uint64_t cancel{};
    };

    bool create(HINSTANCE instance);

    // Point the overlay at a caller-owned counter snapshot, refreshed by the UI
    // thread before each present. Pass nullptr to hide the telemetry line.
    void setCounters(const Counters* counters) { counters_ = counters; }
    void showCardAt(POINT anchor);
    void showIdentity(const HoverTarget& target);
    void hide();
    HWND hwnd() const { return hwnd_; }

    ~OverlayWindow();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void ensureDib(int width, int height);

    // Begin painting a themed, rounded card into the layered DIB. Returns a
    // memory DC with the DIB selected; the caller draws its text lines, then
    // calls endCard(). The DIB carries per-pixel alpha (transparent outside the
    // rounded rect) so the layered surface is genuinely masked — no square
    // corners survive compositing.
    HDC beginCard(int width, int height, const theme::ThemeTokens& tokens,
                  int radius, HBITMAP& outOld);

    // Apply DWM Win11 attributes (rounded corners, immersive dark mode, system
    // backdrop material) and composite the DIB as a layered, click-through
    // surface. Cleans up the DC and font afterwards. `radius` is the corner
    // radius in device px; `surfaceAlpha` is the *background* alpha (255 = opaque
    // readable fallback, <255 lets a mica/acrylic backdrop read through). Text
    // and border pixels are forced to full opacity by maskRoundedCorners so the
    // card stays legible over a bright acrylic material in either theme.
    void endCard(HDC memdc, HBITMAP old, int width, int height, int x, int y,
                 theme::ThemeMode mode, theme::Backdrop backdrop, int radius,
                 unsigned char surfaceAlpha, const theme::Rgb& bg);

    // Finalize the DIB's alpha channel:
    //   * four corner circles -> alpha 0 (transparent punch-out, masked corners)
    //   * pixels matching the card background color -> alpha = surfaceAlpha
    //     (translucent so the acrylic reads through)
    //   * every other pixel (text, border, hairline) -> alpha 255 (fully opaque,
    //     so the card stays readable in dark mode over bright acrylic)
    // GDI fill/text leave alpha at 0, so without this the whole card composites
    // invisible; with the old blanket pass, text was also dimmed and vanished in
    // dark mode. Splitting by background color fixes both.
    void maskRoundedCorners(int width, int height, int radius,
                           unsigned char surfaceAlpha, const theme::Rgb& bg);

    // Draw a title line (semibold, slightly larger) then return the y offset for
    // the next body line. Gives transient UI the title/body hierarchy Win11
    // Flyout/TeachingTip surfaces have, instead of a flat stack of equal lines.
    int drawTitle(HDC memdc, const wchar_t* text, const RECT& rc) const;

    // OS dark-mode preference read live from the Personalize registry so the
    // HUD re-themes when the user flips the system switch (issue #2 #5).
    static bool systemPrefersDark();

    // Re-present the last visible card under the current theme. Called on
    // WM_SETTINGCHANGE so a system appearance change repaints in place.
    void replay();

    const Counters* counters_{};
    HWND hwnd_{};
    HBITMAP dib_{};        // 32-bit DIB section used as the layered surface
    void* dibBits_{};      // DIB pixel buffer (owned by dib_)
    int width_{};
    int height_{};
    HFONT font_{};         // system UI font for the current card (deleted per present)

    // Last presented geometry/target, retained so a theme change can re-present
    // without waiting for the next dwell.
    bool visible_{};
    POINT lastAnchor_{};
    HoverTarget lastTarget_{};
    bool lastIsIdentity_{};
};
