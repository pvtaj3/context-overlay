# context-overlay

Windows 11 native desktop overlay application (C++).

A topmost, transparent, **click-through, non-activating** overlay that shows a
HUD near the cursor when the pointer dwells on a UI element. It resolves the
element's stable identity off-thread (UI Automation) and presents it without
ever stealing focus or input.

## Phases

- **Phase 1** — Layered transparent click-through window shell + mouse dwell
  coordinator.
- **Phase 2** — Stable-hover UIA identity with generation arbitration + dwell
  telemetry.
- **Phase 3 (next)** — UIA text grabber (TextPattern / ValuePattern) + worker
  pipeline.
- **Phase 5+** — Direct2D / DirectWrite card renderer (replaces the current GDI
  card).

## Windows 11 / WinUI styling (see issue #2)

The HUD is themed to blend with the native Windows 11 / WinUI design language
while preserving the click-through / non-activation behaviour.

| Concern | Approach |
| --- | --- |
| Surface | Native Win11 colour/neutral palette via renderer-independent tokens (`src/theme_tokens.h`). |
| Backdrop | DWM `DWMWA_SYSTEMBACKDROP_TYPE` → **mica** when supported, **acrylic** selectable, **opaque solid fallback** when unavailable. |
| Corners | DWM `DWMWA_WINDOW_CORNER_PREFERENCE` = rounded, plus a per-pixel-alpha mask on the layered DIB so the surface is genuinely rounded (no square corners). |
| Typography | System UI font stack (`NONCLIENTMETRICS.lfMessageFont`, i.e. Segoe UI on Win11) instead of the default GDI font. |
| Light / Dark | Follows the OS app theme via `Software\...\Themes\Personalize\AppsUseLightTheme`, re-presenting live on `WM_SETTINGCHANGE`. |
| DPI | Per-monitor v2 awareness; corner radius and font scale with per-monitor DPI. |
| Contrast | Both modes meet WCAG AA (>= 4.5:1) for card text; no hard-coded dark-only colours. |

**Runtime / capability requirements.** All Win11 DWM attributes are resolved
*dynamically* through `GetProcAddress`/`DwmSetWindowAttribute`, so the same
sources build against older SDKs and run on down-level systems:

- Windows 11 — full mica/acrylic + rounded corners + immersive dark mode.
- Windows 10 / composition-disabled — graceful fallback: the GDI card's own
  opaque, rounded, themed surface (acrylic/mica simply is not applied; text
  colours are unchanged, so contrast is preserved).
- Backdrop capability is negotiated at present time (`effectiveBackdrop`): if a
  material cannot be supplied, the card stays opaque and readable.

**Click-through guarantee is preserved.** `WM_NCHITTEST` returns `HTTRANSPARENT`
and `WM_MOUSEACTIVATE` returns `MA_NOACTIVATE`; the `WS_EX_LAYERED |
WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW`
semantics are untouched.

## Building

Windows only. Requires a C++20 toolchain (MSVC or MinGW-w64) and the Windows
SDK.

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The `context_overlay` GUI executable is only configured on `WIN32` hosts. A
non-Windows host can still configure the project to build and run the
platform-free unit tests.

## Tests

```bat
ctest --test-dir build --output-on-failure
```

- `identity_hash` — element-identity hash collision regression (host-testable,
  no Windows needed).
- `theme_tokens` — Win11 theme palette contrast, theme selection/fallback, and
  DPI corner-radius scaling (host-testable, no Windows needed).

The Windows-only renderer (DWM attributes, layered compositing, UIA) is
**compile-verified by construction and must be runtime-validated on real
Windows 11** (no compiler is available in the CI host used to author it).

## Collaboration

Branch-per-feature with pull requests for cross-agent review (see
`CONTRIBUTING.md`). Issue **#2** tracks the native WinUI styling work.
