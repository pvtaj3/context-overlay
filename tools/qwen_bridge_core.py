"""Testable resilience core for the local Qwen/web bridge."""
import json
import time
from urllib.parse import quote

MAX_QUERY = 512
MAX_RESULTS = 5
MAX_SNIPPET = 1200

def sanitize_query(query):
    value = " ".join(str(query).split()).strip()
    if not value:
        raise ValueError("query is empty")
    if len(value) > MAX_QUERY:
        value = value[:MAX_QUERY].rstrip()
    return value

def build_search_url(base, query, limit=MAX_RESULTS):
    q = sanitize_query(query)
    return f"{base}?q={quote(q, safe='')}&limit={max(1, min(limit, MAX_RESULTS))}"

def build_context(results):
    rows = []
    for index, result in enumerate(list(results)[:MAX_RESULTS], 1):
        if not isinstance(result, dict):
            continue
        title = str(result.get("title", ""))[:256]
        url = str(result.get("url", ""))[:1024]
        snippet = str(result.get("snippet", ""))[:MAX_SNIPPET]
        rows.append(f"[{index}] {title}\n{url}\n{snippet}")
    return "\n\n".join(rows)

def build_chat_request(query, results, model="qwen"):
    question = sanitize_query(query)
    context = build_context(results)
    system = ("Use only the supplied web context when answering. Cite sources as "
              "[1], [2], etc. If it is insufficient, say so.\n\n" + context)
    return {"model": str(model)[:128], "temperature": 0.2,
            "messages": [{"role": "system", "content": system},
                         {"role": "user", "content": question}]}

def retry_delays(attempts=3, base=0.25):
    return [base * (2 ** i) for i in range(max(0, min(attempts, 5)))]
