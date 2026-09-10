#!/usr/bin/env python3
"""Small sidecar: search the web, inject concise context, and call llama.cpp.

Run llama.cpp's llama-server locally, for example on a 3090, then set
LLAMA_CPP_URL and SEARCH_URL. The search endpoint must return JSON with a
results array containing title, url, and snippet fields.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

LLAMA_CPP_URL = os.getenv("LLAMA_CPP_URL", "http://127.0.0.1:8080/v1/chat/completions")
SEARCH_URL = os.getenv("SEARCH_URL", "http://127.0.0.1:8787/search")
MODEL = os.getenv("LLAMA_MODEL", "qwen")

def get_json(url, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=8) as response:
        return json.load(response)

def search(query):
    url = SEARCH_URL + "?" + urllib.parse.urlencode({"q": query, "limit": 5})
    result = get_json(url)
    return result.get("results", [])[:5]

def main():
    query = " ".join(sys.argv[1:]).strip()
    if not query:
        raise SystemExit("usage: qwen_web_bridge.py QUERY")
    results = search(query)
    context = "\n\n".join(
        f"[{i + 1}] {r.get('title', '')}\n{r.get('url', '')}\n{r.get('snippet', '')}"
        for i, r in enumerate(results)
    )
    prompt = ("Answer using the supplied web context. Cite sources as [1], [2], etc. "
              "If the context is insufficient, say so.\n\nWeb context:\n" + context)
    response = get_json(LLAMA_CPP_URL, {
        "model": MODEL,
        "temperature": 0.2,
        "messages": [{"role": "system", "content": prompt},
                     {"role": "user", "content": query}],
    })
    print(response["choices"][0]["message"]["content"])

if __name__ == "__main__":
    main()
