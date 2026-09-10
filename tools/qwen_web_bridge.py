#!/usr/bin/env python3
"""Bounded, retrying localhost search -> llama.cpp/Qwen bridge."""
import json, os, sys, time, urllib.request
from qwen_bridge_core import build_chat_request, build_search_url, retry_delays

LLAMA_CPP_URL = os.getenv("LLAMA_CPP_URL", "http://127.0.0.1:8080/v1/chat/completions")
SEARCH_URL = os.getenv("SEARCH_URL", "http://127.0.0.1:8787/search")
MODEL = os.getenv("LLAMA_MODEL", "qwen")
TIMEOUT = max(0.1, min(float(os.getenv("BRIDGE_TIMEOUT", "8")), 30.0))

class BridgeError(RuntimeError): pass

def request_json(url, payload=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as response:
        if response.status < 200 or response.status >= 300: raise BridgeError(f"HTTP {response.status}")
        return json.load(response)

def retry_request(url, payload=None):
    last = None
    for delay in retry_delays(int(os.getenv("BRIDGE_RETRIES", "3"))):
        try: return request_json(url, payload)
        except (OSError, ValueError, BridgeError) as exc:
            last = exc
            time.sleep(delay)
    raise BridgeError(f"request failed after bounded retries: {last}")

def run(query):
    search = retry_request(build_search_url(SEARCH_URL, query))
    results = search.get("results", []) if isinstance(search, dict) else []
    response = retry_request(LLAMA_CPP_URL, build_chat_request(query, results, MODEL))
    try: return response["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as exc: raise BridgeError("invalid llama.cpp response") from exc

def main():
    try: print(run(" ".join(sys.argv[1:])))
    except (BridgeError, ValueError) as exc:
        print(f"qwen bridge unavailable: {exc}", file=sys.stderr); return 2
    return 0

if __name__ == "__main__": raise SystemExit(main())
