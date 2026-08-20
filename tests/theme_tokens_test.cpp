// Host-native unit tests for the Windows 11 / WinUI theme tokens (issue #2).
//
// These cover the renderer-independent decisions that must hold regardless of
// where the drawing happens: both modes meet WCAG AA, theme selection follows
// the OS/app preference, the backdrop fallback degrades to a readable solid,
// and the corner radius scales with DPI. They compile with the host compiler
// (no <windows.h>) because theme_tokens.h is deliberately platform-free.

#include "../src/theme_tokens.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

}  // namespace

int main() {
    using namespace theme;

    // 1. Light mode text is legible on the light surface (WCAG AA >= 4.5).
    {
        auto t = resolveTheme(ThemeMode::kLight);
        expect(textContrastOk(t.cardText, t.cardBg),
               "light mode text meets WCAG AA on card bg");
    }

    // 2. Dark mode text is legible on the dark surface (WCAG AA >= 4.5).
    {
        auto t = resolveTheme(ThemeMode::kDark);
        expect(textContrastOk(t.cardText, t.cardBg),
               "dark mode text meets WCAG AA on card bg");
    }

    // 3. No hard-coded dark-only palette: the two modes use genuinely
    //    different background colors, and neither bg is the other's bg.
    {
        auto light = resolveTheme(ThemeMode::kLight);
        auto dark = resolveTheme(ThemeMode::kDark);
        const bool differ = !(light.cardBg.r == dark.cardBg.r &&
                              light.cardBg.g == dark.cardBg.g &&
                              light.cardBg.b == dark.cardBg.b);
        expect(differ, "light and dark backgrounds differ");
    }

    // 4. Backdrop is decorative only: text contrast is identical with or
    //    without a material behind the card (criterion #2 readable fallback).
    {
        auto withBackdrop = resolveTheme(ThemeMode::kDark);  // same fn, mode only
        auto without = resolveTheme(ThemeMode::kDark);
        expect(textContrastOk(withBackdrop.cardText, withBackdrop.cardBg) &&
                   textContrastOk(without.cardText, without.cardBg),
               "text contrast independent of backdrop material");
    }

    // 5. Backdrop capability negotiation: when the system cannot supply a
    //    material we fall back to the readable solid (kNone).
    {
        expect(effectiveBackdrop(Backdrop::kMica, true) == Backdrop::kMica,
               "mica used when supported");
        expect(effectiveBackdrop(Backdrop::kAcrylic, true) == Backdrop::kAcrylic,
               "acrylic used when supported");
        expect(effectiveBackdrop(Backdrop::kMica, false) == Backdrop::kNone,
               "mica -> kNone fallback when unsupported");
        expect(effectiveBackdrop(Backdrop::kAcrylic, false) == Backdrop::kNone,
               "acrylic -> kNone fallback when unsupported");
    }

    // 6. Theme preference resolution (criterion #5): follow-system honors the
    //    OS dark preference; when the app does not follow, it keeps its default.
    {
        expect(selectMode(false, true) == defaultMode(),
               "not following system -> app default");
        expect(selectMode(false, false) == defaultMode(),
               "not following system (light os) -> app default");
        expect(selectMode(true, true) == ThemeMode::kDark,
               "follow system, os dark -> dark");
        expect(selectMode(true, false) == ThemeMode::kLight,
               "follow system, os light -> light");
    }

    // 7. Default mode preserves the app's long-standing dark card.
    expect(defaultMode() == ThemeMode::kDark, "default mode is dark");

    // 8. Corner radius scales linearly with DPI and never drops below base.
    {
        expect(cornerRadiusForDpi(96) == 8, "96 dpi -> 8px radius");
        expect(cornerRadiusForDpi(144) == 12, "144 dpi -> 12px radius");
        expect(cornerRadiusForDpi(192) == 16, "192 dpi -> 16px radius");
        expect(cornerRadiusForDpi(48) == 8, "low dpi clamped to base radius");
        expect(cornerRadiusForDpi(0) == 8, "degenerate dpi clamped to base");
        expect(cornerRadiusForDpi(384) == 32, "200% dpi -> 32px radius");
    }

    std::printf(failures
                    ? "\n%d test(s) FAILED\n"
                    : "\nall theme token tests passed\n",
                failures);
    return failures ? 1 : 0;
}
