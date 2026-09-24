"""DashScope (Aliyun Model Studio) OpenAI-compatible chat client for translation drafts.

Credentials come from an env file with DASHSCOPE_API_KEY and DASHSCOPE_BASE_URL:
--env-file, else $SRW64_DASHSCOPE_ENV, else the repository's git-ignored .env.
The key is only sent to the configured *.aliyuncs.com HTTPS endpoint.
"""
from __future__ import annotations

import json
import os
import time
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODEL = "deepseek-v4.1-flash"
# CNY per million tokens (input, output, cached input) at the busy-hour list price; idle hours are half.
# Source: Model Studio price list as recorded by the SRW Z SD run on 2026-09-18.
PRICES = {"deepseek-v4.1-flash": (2.0, 8.0, 0.2)}


@dataclass
class Call:
    text: str
    model: str
    finish_reason: str | None
    prompt_tokens: int
    completion_tokens: int
    cached_tokens: int
    elapsed: float


def load_env(path: Path | None = None) -> tuple[str, str]:
    candidates = [path] if path else [Path(os.environ["SRW64_DASHSCOPE_ENV"])] if os.environ.get(
        "SRW64_DASHSCOPE_ENV") else [ROOT / ".env"]
    env: dict[str, str] = {}
    for candidate in candidates:
        if candidate and candidate.exists():
            for line in candidate.read_text().splitlines():
                line = line.strip().removeprefix("export ")
                if "=" in line and not line.startswith("#"):
                    key, value = line.split("=", 1)
                    env[key.strip()] = value.strip().strip("\"'")
    if "DASHSCOPE_API_KEY" not in env or "DASHSCOPE_BASE_URL" not in env:
        raise SystemExit("DashScope credentials not found: pass --env-file or set SRW64_DASHSCOPE_ENV")
    url = urllib.parse.urlsplit(env["DASHSCOPE_BASE_URL"])
    if url.scheme != "https" or not (url.hostname or "").endswith(".aliyuncs.com") or url.query or url.username:
        raise SystemExit("DASHSCOPE_BASE_URL must be an https *.aliyuncs.com endpoint")
    return env["DASHSCOPE_BASE_URL"].rstrip("/"), env["DASHSCOPE_API_KEY"]


def chat(messages: list[dict], *, credentials: tuple[str, str], model: str = DEFAULT_MODEL,
         max_tokens: int = 16000, timeout: float = 600.0, attempts: int = 6) -> Call:
    base_url, key = credentials
    body = json.dumps({"model": model, "messages": messages, "temperature": 0.1, "max_tokens": max_tokens,
                       "enable_thinking": False, "response_format": {"type": "json_object"}, "stream": False},
                      ensure_ascii=False).encode("utf-8")
    last: Exception | None = None
    for attempt in range(attempts):
        request = Request(base_url + "/chat/completions", data=body, method="POST", headers={
            "Authorization": "Bearer " + key, "Content-Type": "application/json", "Accept": "application/json"})
        started = time.perf_counter()
        try:
            with urlopen(request, timeout=timeout) as response:
                doc = json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")[:600]
            last = RuntimeError(f"DashScope HTTP {exc.code}: {detail}")
            if exc.code not in (429, 500, 502, 503, 504):
                raise last from exc
        except (OSError, URLError, json.JSONDecodeError) as exc:
            last = RuntimeError(f"DashScope request failed: {exc}")
        else:
            choice = doc["choices"][0]
            usage = doc.get("usage") or {}
            return Call(text=choice["message"].get("content") or "", model=doc.get("model", model),
                        finish_reason=choice.get("finish_reason"),
                        prompt_tokens=int(usage.get("prompt_tokens") or 0),
                        completion_tokens=int(usage.get("completion_tokens") or 0),
                        cached_tokens=int((usage.get("prompt_tokens_details") or {}).get("cached_tokens") or 0),
                        elapsed=time.perf_counter() - started)
        time.sleep(min(90, 5 * 2 ** attempt))  # rate limits clear within a minute or two
    raise last  # type: ignore[misc]


def parse_json(text: str) -> dict:
    """The reply's JSON object; if the model wrote several objects back to back, their items are merged."""
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1].rsplit("```", 1)[0]
    decoder, position, docs = json.JSONDecoder(), text.find("{"), []
    while 0 <= position < len(text):
        doc, end = decoder.raw_decode(text, position)
        docs.append(doc)
        position = text.find("{", end)
    if not docs:
        raise json.JSONDecodeError("no JSON object in reply", text, 0)
    if len(docs) == 1:
        return docs[0]
    return {**docs[0], "items": [item for doc in docs for item in (doc.get("items") or [])]}


def cost(model: str, prompt: int, completion: int, cached: int) -> dict | None:
    if model not in PRICES:
        return None
    inp, out, hit = PRICES[model]
    busy = ((prompt - cached) * inp + cached * hit + completion * out) / 1e6
    return {"busy_cny": round(busy, 4), "idle_cny": round(busy / 2, 4)}
