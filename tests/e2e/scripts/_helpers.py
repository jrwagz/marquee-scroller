"""Shared utilities for the SPA Playwright scripts."""

from __future__ import annotations

from pathlib import Path

# Repo-root-relative output directory. Each script writes screenshots /
# logs here; gitignored.
OUT = Path(__file__).resolve().parent.parent / "out"
OUT.mkdir(parents=True, exist_ok=True)

# CSS selectors of input fields that may contain credentials. The redact()
# helper clears their .value before screenshotting so shared diagnostic
# screenshots don't leak secrets.
SENSITIVE_FIELD_IDS = (
    "owm-key",
    "wagfam-key",
    "webpw",
)


def redact(page) -> None:
    """Blank out input values that may hold credentials. Run BEFORE
    `page.screenshot(...)` if the screenshot will be shared externally.
    The change is purely client-side — values aren't posted unless the
    user separately submits the form."""
    page.evaluate(
        """(ids) => {
            for (const id of ids) {
                const el = document.getElementById(id);
                if (el) el.value = "[REDACTED]";
            }
        }""",
        list(SENSITIVE_FIELD_IDS),
    )
