# WinUI / Windows 11 styling — design notes & open questions (for review)

> Handoff doc for design review of PR #3 (feat/winui-native-styling).
> The renderer is runtime-validated on Win11; visuals are "close but not yet
> 'belongs in Windows 11'." Dark mode has a material-level issue — see bottom.

## Research basis (Microsoft Learn)
- Geometry: transient/overlay UI (Flyout, TeachingTip, MenuFlyout, ToolTip) uses an
  **8px corner radius** (OverlayCornerRadius). Long-lived controls use 4px.
- Backdrop material: **Acrylic** (DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW)
  is the documented material for transient/overlay UI. **Mica** (DWMSBT_MAINWINDOW)
  is for long-lived app windows. The card is a transient hover surface → Acrylic.
- Transient framing = translucent material + **1px hairline stroke** (WinUI CardStroke)
  + **drop shadow**.
- UI font = **Segoe UI Variable** (Win11) with ClearType.

## What is implemented (src/overlay_window.cpp, theme_tokens.h)
- Backdrop: DWMSBT_TRANSIENTWINDOW (Acrylic) via DWMWA_SYSTEMBACKDROP_TYPE.
- Border: 1px hairline stroke in a neutral CardStroke tint (light #8D8D8D, dark #5A5A5A),
  thin pen — replaces the old hard blue accent frame.
- Palette: neutral surfaces (light #F3F3F3, dark #2B2B2B); accent kept for title/hash only.
  WCAG AA text contrast verified by theme_tokens_test.
- Font: Segoe UI Variable (fallback Segoe UI) + CLEARTYPE_QUALITY.
- Shadow: CS_DROPSHADOW.
- Hierarchy: drawTitle() — semibold, slightly larger accent title with neutral body lines.
- Alpha model: per-pixel — bg-colored pixels = alpha 210 (acrylic reads through);
  text/border pixels = alpha 255 (always opaque, never dimmed).

## Runtime-validated (CONTEXT_OVERLAY_DIAG on Win11)
- Renders; corner=round; live light/dark via WM_SETTINGCHANGE (both directions).
- IUIAutomation2 timeouts set + enforced (hung-window abort at ~407ms, no freeze).
- Host tests: theme_tokens 13/13, identity_hash 9/9.

## OPEN ISSUE — dark mode looks white / text hard to read
Root cause (from runtime log): **Desktop Acrylic is light-by-default and does NOT follow
the app's dark mode.** DWMWA_USE_IMMERSIVE_DARK_MODE=1 does not darken the acrylic material.
The dark surface (rgb 43,43,43) is composited at only 210/255 (82%) over that bright
acrylic, so ~18% of light material bleeds through → card reads medium-gray/white-ish,
and white body text on it is low-contrast.

Options:
- A. Dark-tinted **host backdrop brush** (DWMWA_USE_HOSTBACKDROPBRUSH with a dark brush) —
   how real Win11 dark flyouts render; most correct, more code.
- B. **Opaque dark surface in dark mode** (surfaceAlpha=255 or solid dark material when
   mode==dark) — simplest; loses show-through in dark mode only.
- C. Different material (Mica Alt / etc.).
- D. Darken the fill more in dark mode.

## Asks for reviewer
1. Pick the dark-mode approach (A/B/C/D) — the blocker for "belongs in Windows 11".
2. Suggest palette / spacing / typography refinements (light mode is "okay" not "there").
3. Land as-is, or hold merge until a dark-mode fix lands first?
