# Robustness additions

The bridge core now sanitizes and bounds queries, result counts, snippets, URLs,
and model names. Retry delays use capped exponential backoff; callers should
retry only connection and timeout failures, never malformed requests.

`src/win32_raii.h` provides move-only cleanup wrappers for window stations,
desktops, and icons. Hotkey registration should treat failure as an explicit
conflict/unregistered state and must always call `UnregisterHotKey` during
window teardown.

The Python core and hotkey-state tests are platform-independent. Run:

```text
python -m unittest discover tools -p 'test_*.py'
```

The local sidecar remains optional and must fail closed: if search or llama.cpp
is unavailable after bounded retries, the overlay should retain its existing
identity card instead of blocking the UI thread.
