# Follow-up PR split

After PR #5, keep changes reviewable in small vertical slices:

1. UIA reliability: provider timeouts, COM lifecycle, and identity arbitration.
2. Reliable local vertical slice: overlay -> bounded bridge -> Qwen response,
   with a localhost mock search/model server and no network dependency in tests.
3. Hotkey/bridge isolation: production hotkey integration and sidecar process
   lifecycle, separately from renderer work.
4. Renderer correctness: premultiplied layered pixels, NULL_BRUSH rendering,
   and Windows 10/11 runtime screenshots.

Each PR should include a reproducible test command and avoid claiming runtime
validation that was not performed on Windows hardware.
