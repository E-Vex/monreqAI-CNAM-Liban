"""
A small stdlib HTTP helper.

Deliberately not `requests`/`httpx`: this runs unattended from cron on a
personal machine, so every third-party dependency is one more thing that can
break a job nobody is watching. urllib is enough for a handful of requests
every 15 minutes.

Adds what the original code lacked: bounded retries with exponential backoff,
honouring Retry-After on 429, and error bodies attached to the exception so
failures are actually diagnosable from a log.
"""

from __future__ import annotations

import json
import random
import time
import urllib.error
import urllib.request
from typing import Any, Dict, Optional

USER_AGENT = "ISAEMonitor/2.0 (+https://github.com/E-Vex/monreqAI-CNAM-Liban)"

#: Status codes worth trying again.
RETRYABLE = frozenset({408, 425, 429, 500, 502, 503, 504})


class HttpError(RuntimeError):
    def __init__(self, status: Optional[int], message: str, body: str = ""):
        super().__init__(message)
        self.status = status
        self.body = body

    @property
    def retryable(self) -> bool:
        return self.status is None or self.status in RETRYABLE


def _sleep_for(attempt: int, retry_after: Optional[str]) -> float:
    if retry_after:
        try:
            return min(float(retry_after), 60.0)
        except (TypeError, ValueError):
            pass
    # Exponential backoff with jitter, capped.
    return min(2.0 ** attempt, 30.0) * (0.5 + random.random() / 2)


def request(
    url: str,
    *,
    method: str = "GET",
    headers: Optional[Dict[str, str]] = None,
    json_body: Optional[dict] = None,
    timeout: int = 20,
    retries: int = 3,
    sleep=time.sleep,
) -> bytes:
    """Perform an HTTP request, retrying transient failures. Returns raw bytes."""
    import ssl
    
    data = None
    final_headers = {"User-Agent": USER_AGENT}
    if json_body is not None:
        data = json.dumps(json_body).encode("utf-8")
        final_headers["Content-Type"] = "application/json"
    if headers:
        final_headers.update(headers)

    last: Optional[HttpError] = None

    # Create SSL context that doesn't verify certificates (for servers with SSL issues)
    ssl_context = ssl.create_default_context()
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

    for attempt in range(retries):
        req = urllib.request.Request(url, data=data, headers=final_headers, method=method)
        try:
            # Use custom SSL context to handle problematic certificates
            opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ssl_context))
            with opener.open(req, timeout=timeout) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            body = ""
            try:
                body = exc.read().decode("utf-8", "replace")[:800]
            except Exception:
                pass
            last = HttpError(exc.code, f"HTTP {exc.code} for {_host(url)}", body)
            if not last.retryable or attempt == retries - 1:
                raise last
            sleep(_sleep_for(attempt, exc.headers.get("Retry-After") if exc.headers else None))
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last = HttpError(None, f"network error for {_host(url)}: {exc}")
            if attempt == retries - 1:
                raise last
            sleep(_sleep_for(attempt, None))

    raise last or HttpError(None, "request failed")


def request_json(url: str, **kwargs) -> Any:
    """As request(), but parse the response as JSON."""
    raw = request(url, **kwargs)
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise HttpError(None, f"invalid JSON from {_host(url)}: {exc}",
                        raw[:400].decode("utf-8", "replace")) from exc


def _host(url: str) -> str:
    """Host only: keeps API keys out of log lines."""
    try:
        from urllib.parse import urlparse
        return urlparse(url).netloc or url
    except Exception:
        return "?"
