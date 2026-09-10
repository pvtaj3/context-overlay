# Context Overlay

Context Overlay is a native Windows desktop HUD written in C++. It presents
short-lived context near the pointer without stealing focus or intercepting the
user's click. The overlay is topmost, layered, click-through, non-activating,
and hidden from Alt-Tab.

The project combines a Win32 window shell, UI Automation identity resolution,
WinUI-inspired light/dark styling, a local Qwen/llama.cpp bridge, and an
optional global hotkey. Network and model work is kept outside the overlay UI
thread so a failed dependency cannot freeze the HUD.

## Features

- Win32 layered overlay with per-pixel alpha and rounded corners.
- Stable UI Automation identity resolution off the UI thread.
- Windows light/dark theme tokens with DPI-scaled geometry.
- Local Qwen inference through llama.cpp's OpenAI-compatible HTTP endpoint.
- Optional web-search context injection through a lightweight Python sidecar.
- Ctrl+Shift+Space global visibility toggle.
- Non-activating topmost behavior preserved during toggling.
- Compositor-friendly animation boundary with a direct show/hide fallback.
- StyleProfile and PreferenceMatcher for deterministic preference resolution.
- RAII cleanup wrappers for selected Win32 handle types.
- Bounded input, result, and prompt sizes with capped exponential retry delays.

## Building

The application requires Windows, a C++20 compiler, CMake 3.24 or newer, and
the Windows SDK. The platform-free tests can be configured on a non-Windows
host.

```text
cmake -S . -B build -DCONTEXT_OVERLAY_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The Windows target links against the Win32, GDI, COM, and registry APIs used by
the overlay. The local Qwen bridge is optional; the core overlay remains useful
when no model or search service is running.

## Local Qwen/llama.cpp web bridge

`tools/qwen_web_bridge.py` is an optional sidecar. It keeps network and model
latency out of the Win32 message loop:

1. It sanitizes the user's query and limits its size.
2. It requests a bounded number of results from a configurable JSON search
   endpoint.
3. It injects only title, URL, and snippet fields into a source-aware prompt.
4. It calls a local llama.cpp OpenAI-compatible chat endpoint.
5. It prints the Qwen response with `[1]`, `[2]`, and similar source references.

Start llama.cpp's `llama-server` with a local Qwen model and GPU offload
settings appropriate for the machine (for example, an RTX 3090). Do not commit
model weights, tokens, or API keys.

Example configuration:

```powershell
$env:LLAMA_CPP_URL = "http://127.0.0.1:8080/v1/chat/completions"
$env:LLAMA_MODEL = "qwen"
$env:SEARCH_URL = "http://127.0.0.1:8787/search"
python tools/qwen_web_bridge.py "Windows overlay accessibility guidance"
```

The search service is expected to return JSON in this shape:

```json
{"results":[{"title":"Example", "url":"https://example.test", "snippet":"..."}]}
```

The testable implementation in `tools/qwen_bridge_core.py` applies these
limits:

- Queries are whitespace-normalized and capped at 512 characters.
- At most five results are injected.
- Titles, URLs, and snippets are individually bounded.
- Search limits are clamped and URLs are encoded.
- Retry delays use capped exponential backoff.
- Empty or malformed input fails closed.

Connection and timeout failures may be retried within the bounded policy.
Malformed requests and invalid responses should not be retried indefinitely. If
search or llama.cpp remains unavailable, the overlay should retain its normal
identity card rather than blocking or crashing.

More details are in `docs/LOCAL-QWEN-WEB-BRIDGE.md` and
`docs/ROBUSTNESS.md`.

## Global hotkey and animation

The optional interaction layer in `src/overlay_interaction.{h,cpp}` registers:

```text
Ctrl+Shift+Space
```

The hotkey toggles overlay visibility through `RegisterHotKey`. It uses
`SWP_NOACTIVATE` and `SW_SHOWNOACTIVATE`, so the overlay remains topmost and
click-through without becoming the foreground window or stealing keyboard
focus. Registration failure is treated as a conflict/unregistered state rather
than assumed to have succeeded. The hotkey is unregistered during teardown.

The visibility function is a compositor-friendly boundary. On systems with
Windows composition, a WinUI/DWM transition can animate the surface; on older
or composition-disabled systems, it falls back to an immediate native show/hide
operation. Both paths preserve the layered, topmost, click-through, and
non-activating window contract. The animation implementation must never move
network or model work onto the UI thread.

See `docs/OVERLAY-HOTKEYS-ANIMATION.md`.

## Robustness and resource safety

The code is designed to fail safely at system boundaries:

- `src/win32_raii.h` supplies move-only cleanup wrappers for HWINSTA, HDESK,
  and HICON resources.
- Hotkey lifecycle explicitly handles registration failure and unregisters on
  window destruction.
- Search input, model names, URLs, result counts, snippets, and generated
  prompt context have bounded sizes.
- HTTP/search and local-model retries use capped exponential backoff.
- Optional services fail closed; the core overlay remains responsive.
- UI operations retain non-activation and click-through flags during fallback.
- No model weights, credentials, or external service secrets belong in the
  repository.

## Style profiles

`src/style_profile.h` contains the platform-neutral `StyleProfile` and
`PreferenceMatcher`. It supports system/light/dark theme selection, density,
motion, corner radius, opacity, typography scale, information density,
provenance, and confidence. Values are normalized and clamped before use so
persisted or imported preferences cannot produce invalid geometry or opacity.

## Running tests

Python bridge tests:

```text
python -m unittest discover tools -p "test_*.py"
```

C++ tests:

```text
cmake -S . -B build -DCONTEXT_OVERLAY_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The C++ test suite includes:

- `identity_hash`: stable UI element identity hashing.
- `theme_tokens`: contrast, theme selection, backdrop fallback, and DPI radius
  behavior.
- `style_profile`: preference normalization, theme matching, and opacity policy.
- `hotkey_state_test`: platform-independent registered, unregistered, and
  conflict state behavior.

The Windows renderer, DWM attributes, UI Automation integration, and actual
hotkey registration should additionally be exercised on a Windows 10/11 test
machine. No CI environment should be assumed to have a llama.cpp server or a
web-search service available.

## Project layout

- `src/overlay_window.*`: Win32 layered HUD and rendering.
- `src/dwell_coordinator.*`: pointer dwell and arbitration.
- `src/uia_identity.*`: UI Automation identity resolution.
- `src/theme_tokens.h`: platform-neutral visual tokens.
- `src/style_profile.h`: preference model and matcher.
- `src/overlay_interaction.*`: hotkey and visibility boundary.
- `src/win32_raii.h`: selected Win32 resource wrappers.
- `tools/qwen_web_bridge.py`: optional local model/search sidecar.
- `tools/qwen_bridge_core.py`: bounded request and prompt builder.
- `tests/`: host-testable C++ coverage.
- `docs/`: runtime, bridge, animation, and robustness notes.

## License

See `LICENSE`.
