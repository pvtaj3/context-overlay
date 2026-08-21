#pragma once

// Native Windows 11 / WinUI visual styling tokens for the HUD.
//
// This header is deliberately free of <windows.h> so the theme selection,
// fallback, and contrast logic can be unit tested on any host (see
// tests/theme_tokens_test.cpp). overlay_window.cpp is the only production
// consumer and converts these platform-neutral tokens into GDI/DWM calls.
//
// Design contract (per issue #2):
//   * Light and dark palettes both meet WCAG AA (>= 4.5:1) for card text.
//   * No hard-coded dark-only colors: every mode carries its own bg/text.
//   * Backdrop (mica/acrylic) is decorative only; disabling it never changes
//     text colors, so readable contrast survives the fallback path.
//   * Corner radius scales with DPI; the math here is host-testable even though
//     the masking happens on Windows.

#include <algorithm>
#include <cmath>

namespace theme {

// Which OS appearance the HUD is drawn for.
enum class ThemeMode { kLight, kDark };

// System backdrop material behind the card. `kNone` is the readable fallback
// (opaque/translucent solid) used when DWM backdrop APIs are unavailable or
// composition is disabled.
enum class Backdrop { kNone, kMica, kAcrylic };

// A packed 0..255 sRGB triple (NOT the Win32 COLORREF byte order; overlay
// window converts with toColorref() so layout assumptions never leak here).
struct Rgb {
    unsigned char r{}, g{}, b{};
};

constexpr Rgb rgb(unsigned char r, unsigned char g, unsigned char b) {
    return Rgb{r, g, b};
}

// Built-in palette for one mode. Text colors are chosen so text-on-bg contrast
// is >= kMinTextContrast in BOTH modes.
//
// The palette mirrors the Win11/WinUI *Card* tokens rather than a generic
// accent rectangle:
//   * cardBg is a near-neutral surface (light: #F3F3F3-ish; dark: #2B2B2B-ish)
//     — deliberately NOT a saturated blue, so the card reads as a system
//     surface, not a branded banner.
//   * cardText is high-contrast neutral text (not pure white/black, which is
//     harsher than WinUI's actual text tokens).
//   * accent is the system-UI blue but only used for the identity hash /
//     emphasis glyph, never the card border.
//   * cardStroke is the faint 1px hairline (WinUI CardStrokeColor) that frames
//     real transient UI (Flyout/TeachingTip/ContextMenu). It is a low-contrast
//     neutral derived from the surface — NOT the old hard blue frame.
struct ThemeTokens {
    Rgb cardBg;       // card surface (also the readable fallback when no backdrop)
    Rgb cardText;     // primary text on the card
    Rgb accent;       // emphasis / hash glyph color (decorative)
    Rgb cardStroke;   // 1px hairline border (WinUI CardStrokeColor)
};

// WCAG relative luminance of an sRGB triple.
inline double channelLinear(unsigned char c) {
    double s = c / 255.0;
    return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

inline double relativeLuminance(Rgb c) {
    return 0.2126 * channelLinear(c.r) + 0.7152 * channelLinear(c.g) +
           0.0722 * channelLinear(c.b);
}

// WCAG contrast ratio between two colors (1.0 .. 21.0).
inline double contrastRatio(Rgb a, Rgb b) {
    double la = relativeLuminance(a);
    double lb = relativeLuminance(b);
    double hi = std::max(la, lb);
    double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

// Minimum contrast for normal-weight body text (WCAG AA).
constexpr double kMinTextContrast = 4.5;

// True when `text` is legible on `bg` per WCAG AA.
inline bool textContrastOk(Rgb text, Rgb bg) {
    return contrastRatio(text, bg) >= kMinTextContrast;
}

// Palette for a given mode. Backdrop does NOT change text colors, so the same
// tokens are returned regardless of which material is behind the card.
inline ThemeTokens resolveTheme(ThemeMode mode) {
    if (mode == ThemeMode::kLight) {
        // Windows 11 light card: neutral surface, near-black text, faint gray
        // stroke, system-blue accent for emphasis only.
        return ThemeTokens{rgb(243, 243, 243), rgb(32, 32, 32),
                           rgb(0, 120, 212), rgb(141, 141, 141)};
    }
    // Dark: Win11 dark card surface (#2B2B2B-ish), near-white text, faint
    // lighter stroke, lighter system-blue accent.
    return ThemeTokens{rgb(43, 43, 43), rgb(242, 242, 242),
                       rgb(96, 165, 250), rgb(90, 90, 90)};
}

// When the system cannot provide a backdrop material, we fall back to an opaque
// solid card. This is a pure decision so it can be unit tested; the actual
// capability probe lives in overlay_window.cpp (Windows/DWM runtime).
inline Backdrop effectiveBackdrop(Backdrop desired, bool systemSupportsBackdrop) {
    return systemSupportsBackdrop ? desired : Backdrop::kNone;
}

// Default appearance when theme-following cannot be determined. Preserves the
// app's long-standing dark card so behaviour does not flip unprompted.
constexpr ThemeMode defaultMode() { return ThemeMode::kDark; }

// Resolve which mode to draw given the user's follow-system setting. When the
// app follows the OS, it matches the OS dark preference; otherwise it uses the
// app default. This is the pure core of criterion #5 (respond to preference).
inline ThemeMode selectMode(bool followSystem, bool systemPrefersDark) {
    if (!followSystem) return defaultMode();
    return systemPrefersDark ? ThemeMode::kDark : ThemeMode::kLight;
}

// Windows 11 card corner radius at 96 DPI (design unit). Theming scales it
// linearly with DPI so corners stay proportional on high-DPI displays.
constexpr int kBaseCornerRadius = 8;

// Corner radius (device px) for a given DPI. At 96 DPI -> 8, 144 -> 12,
// 192 -> 16. Never shrinks below the base, even on low DPI.
inline int cornerRadiusForDpi(int dpi) {
    if (dpi < 1) dpi = 96;
    const double scale = static_cast<double>(dpi) / 96.0;
    const int r = static_cast<int>(std::lround(kBaseCornerRadius * scale));
    return std::max(r, kBaseCornerRadius);
}

}  // namespace theme
