# Hotkey and animation integration

`src/overlay_interaction.{h,cpp}` provides the interaction boundary for the HUD.
It registers Ctrl+Shift+Space with `RegisterHotKey`, handles `WM_HOTKEY` in the
existing UI message loop, and toggles visibility with `SWP_NOACTIVATE` so the
overlay never steals focus.

The current animation function intentionally uses the native compositor-friendly
show/hide path with a safe immediate fallback. A future WinUI Composition-backed
renderer can replace the transition body without changing hotkey behavior or
window semantics. Any compositor animation must retain topmost, layered,
click-through, and non-activating styles.
