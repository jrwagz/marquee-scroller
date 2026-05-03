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
    assert "/updatefs" in body, (
        "the 404 body must surface the OTA path (/updatefs) as the easiest "
        "fix — without it, users on already-deployed devices think they need "
        "a serial cable. This was the resolution to the #63 follow-up "
        "discussion ('is there no way to ship this OTA?')."
    )


def test_updatefs_route_supports_both_get_and_post(ino_source: str) -> None:
    """/updatefs is the OTA path for the SPA bundle on deployed devices.
    GET serves the upload form; POST receives the multipart body and writes
    via Update.begin(fsSize, U_FS). Both must be registered."""
    methods = set(
        re.findall(
            r'server\.on\(\s*"/updatefs"\s*,\s*(HTTP_\w+)',
            ino_source,
        )
    )
    assert "HTTP_GET" in methods, (
        "GET /updatefs must serve the upload form so browsers can hit it directly"
    )
    assert "HTTP_POST" in methods, (
        "POST /updatefs must receive the LittleFS image"
    )


def test_updatefs_post_uses_u_fs_not_u_flash(ino_source: str) -> None:
    """The defining difference between /update and /updatefs is the partition.
    If POST /updatefs's Update.begin uses U_FLASH, it will overwrite the
    sketch — bricking the device and losing everything."""
    # Find the POST /updatefs registration, capture the body of both lambdas.
    block = re.search(
        r'server\.on\("/updatefs",\s*HTTP_POST,(.*?)\}\);',
        ino_source,
        re.DOTALL,
    )
    assert block, "POST /updatefs registration not found"
    body = block.group(1)
    assert "U_FS" in body, "POST /updatefs must call Update.begin(..., U_FS)"
    # Should NOT mention U_FLASH (which would clobber the sketch).
    assert "U_FLASH" not in body, (
        "POST /updatefs must NOT use U_FLASH — that would overwrite the sketch "
        "instead of the filesystem partition"
    )
