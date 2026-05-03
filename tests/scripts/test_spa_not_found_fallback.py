"""
Static-analysis regression test for the /spa-not-installed fallback in
marquee/marquee.ino.

Why this exists:
  Issue #63: a device with firmware that registers `serveStatic("/spa", ...)`
  but no LittleFS bundle (the default state after an OTA-only flash)
  silently 302'd `/spa` to `/`, which made the SPA look broken instead of
  just absent. The fix is a `handleNotFound()` dispatcher that detects
  `/spa*` requests and serves a 404 page with deploy instructions
  (`make uploadfs` or `esptool.py write_flash`).

  This test pins the contract:
    - server.onNotFound() points at handleNotFound, not redirectHome
    - handleNotFound branches on URL prefix and returns 404 for /spa
    - The 404 body mentions both the failure mode and the fix
"""

import re
from pathlib import Path

import pytest

INO = Path(__file__).parent.parent.parent / "marquee" / "marquee.ino"


@pytest.fixture(scope="module")
def ino_source() -> str:
    return INO.read_text(encoding="utf-8")


def test_onnotfound_dispatches_through_handlenotfound(ino_source: str) -> None:
    assert re.search(
        r"server\.onNotFound\(\s*handleNotFound\s*\)",
        ino_source,
    ), "server.onNotFound must call handleNotFound, not redirectHome directly"


def test_handlenotfound_branches_on_spa_prefix(ino_source: str) -> None:
    """The dispatcher must check /spa specifically and respond with 404
    (not 302), and must keep the legacy redirect-home behavior for
    everything else."""
    body_match = re.search(
        r"void handleNotFound\(AsyncWebServerRequest \*request\)\s*\{(.*?)\n\}",
        ino_source,
        re.DOTALL,
    )
    assert body_match, "handleNotFound function not found"
    body = body_match.group(1)
    assert '"/spa"' in body, (
        "handleNotFound must check the /spa URL prefix; otherwise the silent "
        "302 from issue #63 returns."
    )
    assert "404" in body, "the /spa fallback must respond with HTTP 404"
    assert "redirectHome" in body, (
        "handleNotFound must still call redirectHome for non-/spa requests"
    )


def test_spa_404_body_names_the_fix(ino_source: str) -> None:
    """Without naming the fix, the error page is no better than the redirect."""
    body_match = re.search(
        r"void handleNotFound\(AsyncWebServerRequest \*request\)\s*\{(.*?)\n\}",
        ino_source,
        re.DOTALL,
    )
    assert body_match
    body = body_match.group(1)
    assert "uploadfs" in body, (
        "the 404 body should mention `make uploadfs` so the user knows the fix"
    )
    assert "littlefs" in body.lower() or "LittleFS" in body, (
        "the 404 body should explain that LittleFS is the missing piece"
    )
