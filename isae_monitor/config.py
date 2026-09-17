"""
Configuration, parsed and validated exactly once.

The old code read os.environ at import time scattered across the module,
which made the behaviour impossible to test without mutating global state.
Everything now flows through Settings.from_env().
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .departments import DEPARTMENTS, GENERAL

DEFAULT_FEED_URL = "https://annonces.isae.edu.lb/feeds/posts/default?max-results=25"
DEFAULT_GEMINI_MODEL = "gemini-2.5-flash"
DEFAULT_OPENROUTER_MODEL = "meta-llama/llama-3.3-70b-instruct"

# Legacy env var -> department key, so existing setups keep working.
LEGACY_CHANNEL_VARS: Dict[str, str] = {
    "TELEGRAM_CHANNEL_CS": "informatique",
}


class ConfigError(RuntimeError):
    """Raised when configuration is missing or self-contradictory."""


@dataclass
class Settings:
    feed_url: str = DEFAULT_FEED_URL
    state_file: str = "seen.json"

    gemini_keys: List[str] = field(default_factory=list)
    gemini_model: str = DEFAULT_GEMINI_MODEL
    openrouter_key: Optional[str] = None
    openrouter_model: str = DEFAULT_OPENROUTER_MODEL

    telegram_token: Optional[str] = None
    general_channel: Optional[str] = None
    #: department key -> telegram chat id (only those actually configured)
    department_channels: Dict[str, str] = field(default_factory=dict)

    request_timeout: int = 20
    max_retries: int = 3
    #: seconds between outbound Telegram sends, to stay under the rate limit
    send_interval: float = 1.2
    #: how many announcement ids to retain in the state file
    state_history: int = 1000

    @classmethod
    def from_env(cls, env: Optional[Dict[str, str]] = None) -> "Settings":
        e = os.environ if env is None else env

        def get(name: str, default: str = "") -> str:
            return (e.get(name) or default).strip()

        channels: Dict[str, str] = {}
        for dept in DEPARTMENTS:
            value = get(dept.env_var)
            if value:
                channels[dept.key] = value

        # Honour the old single-department variable if the new one is absent.
        for legacy_var, dept_key in LEGACY_CHANNEL_VARS.items():
            legacy_value = get(legacy_var)
            if legacy_value and dept_key not in channels:
                channels[dept_key] = legacy_value

        keys = [k.strip() for k in get("GEMINI_API_KEYS").split(",") if k.strip()]
        # Tolerate the singular form; people get this wrong constantly.
        if not keys and get("GEMINI_API_KEY"):
            keys = [get("GEMINI_API_KEY")]

        return cls(
            feed_url=get("FEED_URL", DEFAULT_FEED_URL),
            state_file=get("STATE_FILE", "seen.json"),
            gemini_keys=keys,
            gemini_model=get("GEMINI_MODEL", DEFAULT_GEMINI_MODEL),
            openrouter_key=get("OPENROUTER_API_KEY") or None,
            openrouter_model=get("OPENROUTER_MODEL", DEFAULT_OPENROUTER_MODEL),
            telegram_token=get("TELEGRAM_BOT_TOKEN") or None,
            general_channel=get("TELEGRAM_CHANNEL_GENERAL") or None,
            department_channels=channels,
            request_timeout=int(get("REQUEST_TIMEOUT", "20")),
            max_retries=int(get("MAX_RETRIES", "3")),
            send_interval=float(get("SEND_INTERVAL", "1.2")),
            state_history=int(get("STATE_HISTORY", "1000")),
        )

    # --- Capability checks ---------------------------------------------------

    @property
    def has_ai(self) -> bool:
        return bool(self.gemini_keys or self.openrouter_key)

    @property
    def has_telegram(self) -> bool:
        return bool(self.telegram_token and (self.general_channel or self.department_channels))

    def problems(self) -> List[str]:
        """Non-fatal configuration gaps worth warning about."""
        out: List[str] = []
        if not self.telegram_token:
            out.append("TELEGRAM_BOT_TOKEN is not set: nothing will be delivered.")
        elif not self.general_channel and not self.department_channels:
            out.append("No channels configured: set TELEGRAM_CHANNEL_GENERAL "
                       "and/or per-department channels.")
        if not self.has_ai:
            out.append("No AI keys (GEMINI_API_KEYS / OPENROUTER_API_KEY): "
                       "falling back to keyword classification, which is rough.")
        if not self.department_channels:
            out.append("No department channels configured: every announcement "
                       "will only reach the general channel.")
        return out

    def summary(self) -> str:
        configured = ", ".join(sorted(self.department_channels)) or "none"
        providers = []
        if self.gemini_keys:
            providers.append(f"gemini x{len(self.gemini_keys)} ({self.gemini_model})")
        if self.openrouter_key:
            providers.append(f"openrouter ({self.openrouter_model})")
        return (
            f"feed      : {self.feed_url}\n"
            f"state     : {self.state_file}\n"
            f"providers : {', '.join(providers) or 'keyword fallback only'}\n"
            f"general   : {'configured' if self.general_channel else 'not set'}\n"
            f"departments: {configured}"
        )
