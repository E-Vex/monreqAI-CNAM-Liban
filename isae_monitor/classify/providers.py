"""
LLM providers for classification.

The original implementation was broken in ways that were invisible at runtime:

  * it pinned gemini-1.5-flash, which Google has shut down; every request now
    returns 404, so the whole Gemini phase was dead;
  * it capped maxOutputTokens at 20. Current Gemini Flash models think before
    answering, and thinking tokens count against that budget, so the response
    came back with no text at all and was silently read as "other";
  * it indexed candidates[0].content.parts[0].text unguarded, so a safety
    block or a MAX_TOKENS finish raised IndexError/KeyError -- neither of
    which the caller caught, killing the entire run;
  * it set temperature, which current Gemini models have deprecated.

Both providers are asked for structured JSON so the answer can be parsed
exactly rather than regex-scraped out of prose.
"""

from __future__ import annotations

import json
from typing import List, Optional

from .. import httpclient
from ..departments import VALID_CATEGORIES, resolve

GEMINI_ENDPOINT = (
    "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
)
OPENROUTER_ENDPOINT = "https://openrouter.ai/api/v1/chat/completions"

#: Enough headroom that thinking tokens cannot starve the actual answer.
MAX_OUTPUT_TOKENS = 512


class ProviderError(RuntimeError):
    def __init__(self, message: str, status: Optional[int] = None):
        super().__init__(message)
        self.status = status


def _extract_category(text: str) -> Optional[str]:
    """
    Pull a category out of a model response.

    Tries strict JSON first, then a bare token. Unlike the original
    last-regex-match heuristic, this never infers a category from a word
    appearing mid-sentence, so "not general or other" no longer resolves
    to "other".
    """
    if not text:
        return None
    cleaned = text.strip().strip("`")
    if cleaned.lower().startswith("json"):
        cleaned = cleaned[4:].strip()

    try:
        data = json.loads(cleaned)
        if isinstance(data, dict):
            return resolve(str(data.get("category", "")))
        if isinstance(data, str):
            return resolve(data)
    except (json.JSONDecodeError, TypeError):
        pass

    # Not JSON: accept only if the whole response is one known label.
    token = cleaned.strip().strip('"').strip(".").lower()
    if token in VALID_CATEGORIES:
        return token
    return resolve(token)


# --- Gemini ------------------------------------------------------------------

def call_gemini(prompt: str, api_key: str, model: str, timeout: int, retries: int) -> str:
    """Call Gemini and return the raw text answer."""
    payload = {
        "contents": [{"role": "user", "parts": [{"text": prompt}]}],
        "generationConfig": {
            "maxOutputTokens": MAX_OUTPUT_TOKENS,
            "responseMimeType": "application/json",
            "responseSchema": {
                "type": "OBJECT",
                "properties": {"category": {"type": "STRING",
                                            "enum": list(VALID_CATEGORIES)}},
                "required": ["category"],
            },
            # Keep reasoning short: this is a labelling task, not a puzzle.
            "thinkingConfig": {"thinkingLevel": "low"},
        },
    }
    url = GEMINI_ENDPOINT.format(model=model)
    headers = {"x-goog-api-key": api_key}   # header, not query string, so the
                                            # key cannot leak via logged URLs

    try:
        data = httpclient.request_json(url, method="POST", headers=headers,
                                       json_body=payload, timeout=timeout,
                                       retries=retries)
    except httpclient.HttpError as exc:
        # Older / smaller models reject thinkingConfig or responseSchema with
        # a 400. Retry once with a plain request rather than losing the key.
        if exc.status == 400:
            payload["generationConfig"] = {"maxOutputTokens": MAX_OUTPUT_TOKENS}
            try:
                data = httpclient.request_json(url, method="POST", headers=headers,
                                               json_body=payload, timeout=timeout,
                                               retries=retries)
            except httpclient.HttpError as retry_exc:
                raise ProviderError(str(retry_exc), retry_exc.status) from retry_exc
        else:
            raise ProviderError(str(exc), exc.status) from exc

    return _read_gemini_text(data)


def _read_gemini_text(data: dict) -> str:
    """Defensively read the text out of a Gemini response."""
    if not isinstance(data, dict):
        raise ProviderError("unexpected Gemini response shape")

    if "promptFeedback" in data and data["promptFeedback"].get("blockReason"):
        raise ProviderError(f"blocked: {data['promptFeedback']['blockReason']}")

    candidates = data.get("candidates") or []
    if not candidates:
        raise ProviderError("Gemini returned no candidates")

    candidate = candidates[0]
    parts = (candidate.get("content") or {}).get("parts") or []
    text = "".join(p.get("text", "") for p in parts if isinstance(p, dict))

    if not text.strip():
        reason = candidate.get("finishReason", "unknown")
        raise ProviderError(f"Gemini returned empty text (finishReason={reason})")
    return text


# --- OpenRouter ---------------------------------------------------------------

def call_openrouter(prompt: str, api_key: str, model: str, timeout: int, retries: int) -> str:
    payload = {
        "model": model,
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": MAX_OUTPUT_TOKENS,
        "temperature": 0,
        "response_format": {"type": "json_object"},
    }
    headers = {
        "Authorization": f"Bearer {api_key}",
        # OpenRouter uses these for attribution on its dashboard.
        "HTTP-Referer": "https://github.com/E-Vex/monreqAI-CNAM-Liban",
        "X-Title": "ISAE Announcements Monitor",
    }
    try:
        data = httpclient.request_json(OPENROUTER_ENDPOINT, method="POST",
                                       headers=headers, json_body=payload,
                                       timeout=timeout, retries=retries)
    except httpclient.HttpError as exc:
        raise ProviderError(str(exc), exc.status) from exc

    choices = data.get("choices") or []
    if not choices:
        raise ProviderError(f"OpenRouter returned no choices: {str(data)[:200]}")
    text = (choices[0].get("message") or {}).get("content") or ""
    if not text.strip():
        raise ProviderError("OpenRouter returned empty content")
    return text
