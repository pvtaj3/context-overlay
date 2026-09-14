# WinUI native styling — runtime validation (issue #2)

Companion to `PHASE2-RUNTIME-TEST-PLAN.md`. This covers the native Windows 11
/ WinUI visual styling added in PR #3 (`feat/winui-native-styling`), on top of
the Phase 2 binary.

The renderer changes — DWM attributes, per-pixel-alpha rounded compositing, the
system font, and the live light/dark re-present — were **compile-and-review
verified only** (no MSVC/MinGW on the authoring host). This plan is how to
confirm them on real Windows 11.

**Run with diagnostics.** Use `RUN-WITH-DIAGNOSTICS.bat` (sets
`CONTEXT_OVERLAY_DIAG=1`, writes `%LOCALAPPDATA%\context-overlay\diag.log`,
also mirrored to OutputDebugStringW for DebugView). Every item below lists what
to look for in that log.

To quit: no tray/hotkey yet — `taskkill /IM context_overlay.exe /F`.

---

## 1. Rounded corners are genuinely masked (no square corners)

The bug risk: a layered window with `UpdateLayeredWindow` keeps square corners
unless the bitmap itself carries per-pixel alpha outside the rounded rect. We set
alpha=255 inside and 0 in the corner circles (`maskRoundedCorners`).

- [ ] Launch, hover Notepad text, hold ~0.5s. Card appears.
      Expect: the **card's four corners are rounded**, matching other Win11
      surfaces (Settings, Explorer tooltip). No hard square corners at 96 DPI.
- [ ] Open `diag.log`. Expect a line:
      `present: mode=… dpi=96 radius=8 alpha=… size=… pos=…`
      Confirms radius (8 at 96 DPI) and that the masked present path ran.
- [ ] At 150% display scaling (Settings → Display → 144 DPI): re-launch and
      hover. Expect: corners still rounded and proportional; log shows
      `dpi=144 radius=12`.
- [ ] At 200% (192 DPI): expect `radius=16`.

Pass = visibly rounded corners at 100/150/200% scaling, radius matches DPI in
the log, no square corners at any scale.

---

## 2. DWM attributes actually apply (rounded + immersive dark)

`applyWin11Backdrop` resolves `DwmSetProperty`/`DwmSetWindowAttribute` at
runtime, so an absent API must degrade silently.

- [ ] On Windows 11: launch, hover. Expect a `win11:` line in the log:
      `win11: corner=round immersive=<0|1> backdrop=mica`
- [ ] With the OS in **dark** mode: expect `immersive=1`. In **light** mode:
      expect `immersive=0`. (The card reads the OS app theme from
      `Personalize\AppsUseLightTheme`.)
- [ ] Inspect the window in a tool that shows DWM attributes (e.g. Spy++ or
      `Dwmspy` from the Windows SDK) — confirm `DWMWA_WINDOW_CORNER_PREFERENCE`
      is set to rounded and `DWMWA_USE_IMMERSIVE_DARK_MODE` matches the mode.

Pass = `win11:` line present with the expected flags, and DWM inspector agrees.

**Fallback check (down-level / failure):** if `DwmSetWindowAttribute` is
unavailable the log shows
`win11: DwmSetWindowAttribute unavailable; GDI fallback (square corners, opaque card)`
and the app must still run with a square, opaque, readable card. Acceptable on
Win10/disabled composition; FAIL only if Win11 shows this line.

---

## 3. Mica / acrylic backdrop (and opaque fallback)

We request `DWMWA_SYSTEMBACKDROP_TYPE = mica` when supported.

- [ ] On Windows 11 with a supported desktop (no basic theme): the card should
      show the **mica** material behind the surface — a subtle desktop-tinted
      backdrop, not a flat fill — when alpha < 255 (we ship `alpha=235` so it
      reads through slightly).
- [ ] The log records `backdrop=mica`.
- [ ] **Light contrast must hold:** even with mica behind it, the card text
      stays legible. This is guaranteed by the token palette (both modes pass
      WCAG AA in `theme_tokens_test`), but confirm by eye in both themes.
- [ ] **Fallback:** if a build runs where mica is unavailable (basic theme, RDP
      to a host without the material, or a deliberate `effectiveBackdrop(…,false)`),
      the card must still be **opaque and readable** (`alpha=255`), not
      transparent-to-the-point-of-unreadable. No log line should show the card
      vanishing.

Pass = mica visible on Win11, text legible over it; opaque readable fallback
when material unavailable.

> Note: the binary currently always requests `mica` (`effectiveBackdrop(kMica,
> true)`). Acrylic is selectable via the `Backdrop` enum but not wired into a
> switch yet — covering acrylic is a future tweak, not a regression target here.

---

## 4. System font stack replaces the default GDI font

- [ ] Hover a control. Expect the card text in **Segoe UI** (the OS UI font),
      not the blocky default GDI font previously used. Compare weight/size/spacing
      to a native Win11 tooltip.
- [ ] At 150% / 200% scaling, text must not blur or mis-size — it should scale
      with DPI (the font is built from `NONCLIENTMETRICS.lfMessageFont`, which is
      DPI-aware, and the DC is PMv2-aware).
- [ ] Both light and dark: text contrast legible. The dark card uses
      `rgb(240,240,240)` on `rgb(32,32,32)`; light uses `rgb(32,32,32)` on
      `rgb(245,245,247)`. These are unit-tested for >= 4.5:1 but confirm by eye.

Pass = Segoe UI, DPI-correct, legible in both themes.

---

## 5. Live light/dark switching responds to the OS

- [ ] Launch with OS in dark mode. Hover → dark card (`immersive=1`,
      `mode=dark` in log).
- [ ] **Without closing the app**, open Settings → Personalization → Colors and
      switch "Choose your default app mode" dark ↔ light.
- [ ] Expect: the **already-visible card repaints** under the new theme
      immediately (or on the next dwell). The log shows
      `WM_SETTINGCHANGE received; re-presenting under new theme` followed by a
      `present: mode=…` line with the new mode.
- [ ] If the card was hidden when you switched, the next hover shows the new
      theme (no stale theme stuck).

Pass = theme follows the OS live, both directions, with a matching
`WM_SETTINGCHANGE` log line.

---

## 6. Click-through / non-activation preserved (no regression)

These are the Phase 1/2 guarantees that must NOT break under the new renderer.

- [ ] Thrash the cursor across many controls for ~60s (see Phase 2 item 1).
      Expect: cards appear/leave correctly, **no focus theft** — keep typing in
      Notepad and verify every keystroke lands there.
- [ ] `WM_NCHITTEST` returns `HTTRANSPARENT`, `WM_MOUSEACTIVATE` returns
      `MA_NOACTIVATE` — unchanged. The card is never in the Alt-Tab list
      (`WS_EX_TOOLWINDOW`) and never activates (`WS_EX_NOACTIVATE`).
- [ ] Dwell arbitration + identity presentation still work (re-run Phase 2
      items 1/5 if in doubt).

Pass = no focus theft, no activation, identity pipeline intact.

---

## Known observability gap

The WinUI path is now traced (`win11:` and `present:` lines), so all six items
are observable from the build + log. The one thing the log does **not** prove is
*visual* correctness of mica vs flat fill — that needs a human eye (item 3). The
`alpha=235` vs `255` value in the log is the tell: `235` means "material expected
behind", `255` means "opaque fallback".

---

## Reporting back

For each item: PASS / FAIL / BLOCKED, plus what you actually observed (quote the
relevant `diag.log` lines). For the visual items (1, 3, 4) a screenshot is worth
more than prose. Anything that fails, note the Windows build + display scaling +
theme, since DWM behaviour varies by build (22H2 vs 24H2) and whether the desktop
is in "personalized" vs "contrast/High-Contrast" mode — High-Contrast mode
forcibly overrides backdrop/rounded styling and is a valid fallback, not a bug.
