## Summary
<!-- What changed and why. -->

## Linked milestone
<!-- Notion phase/milestone, e.g. Phase 2 — Dwell Telemetry -->

## Changes
-

## Build & verification
- [ ] Compiles with MSVC / Visual Studio 2022 on Windows 11
- [ ] Behavioural check on Windows 11 (Chrome, Terminal, Rhino 3D)
- [ ] Click-through + no focus theft still intact
- [ ] MinGW cross-compile gate passes (if changed Win32 usage)

## Reviewer checklist
- [ ] Code reads clean and matches the architecture doc
- [ ] No Windows-SDK-only assumptions that break the MinGW gate
- [ ] No secrets / tokens committed
