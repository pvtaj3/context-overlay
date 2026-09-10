# Local Qwen web-context bridge

`tools/qwen_web_bridge.py` is an optional sidecar for a local llama.cpp server.
It performs a bounded web search through a configurable JSON endpoint, injects
only the returned title/URL/snippet context into a Qwen chat request, and prints
the answer with source references.

Example:

```powershell
$env:LLAMA_CPP_URL = "http://127.0.0.1:8080/v1/chat/completions"
$env:SEARCH_URL = "http://127.0.0.1:8787/search"
python tools/qwen_web_bridge.py "latest Windows overlay API guidance"
```

The sidecar is deliberately decoupled from the Win32 process: web credentials,
network failures, prompt limits, and llama.cpp lifecycle stay outside the HUD.
Run llama-server with the local Qwen model and appropriate GPU offload settings
for the user's RTX 3090. Do not commit model weights or API keys.

The overlay can later consume this bridge through a localhost-only IPC adapter;
the current change keeps the network work off the UI thread and preserves the
existing click-through/non-activating contract.
