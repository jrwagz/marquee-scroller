"""
Static-analysis regression tests for marquee/marquee.ino route registrations.

Why this exists:
  When the web server was migrated from ESP8266WebServer to ESPAsyncWebServer
  (commit fdbc6fb), ESP8266HTTPUpdateServer was dropped. That library used to
  register BOTH GET (file-upload form) and POST (upload handler) on /update,
  but the migration only reimplemented POST. GET /update silently fell through
  to onNotFound → redirectHome, which the user perceived as the page being
  "broken" — see issue #60. These tests pin the route table so a future
  migration or refactor can't drop a method from a multi-method endpoint
  without the test suite noticing.
"""

import re
from pathlib import Path

import pytest

INO = Path(__file__).parent.parent.parent / "marquee" / "marquee.ino"


@pytest.fixture(scope="module")
def ino_source() -> str:
    return INO.read_text(encoding="utf-8")


def _registered_methods(source: str, route: str) -> set[str]:
    """Return the set of HTTP method tokens registered for `route` via server.on(...)."""
    pattern = re.compile(
        r'server\.on\(\s*"' + re.escape(route) + r'"\s*,\s*(HTTP_\w+)',
    )
    return set(pattern.findall(source))


def test_update_route_supports_both_get_and_post(ino_source: str) -> None:
    """GET /update must serve the upload form; POST /update receives the binary.

    Regression: dropping the GET handler makes /update redirect to home (issue #60).
    """
    methods = _registered_methods(ino_source, "/update")
    assert "HTTP_GET" in methods, (
        "GET /update handler missing — visiting /update in a browser will "
        "redirect to home instead of showing the upload form. See issue #60."
    )
    assert "HTTP_POST" in methods, "POST /update handler missing"


def test_update_form_posts_back_to_update(ino_source: str) -> None:
    """The upload form must target /update with multipart/form-data, or the
    upload chunk handler will never receive the body."""
    assert "UPLOAD_FORM" in ino_source, "UPLOAD_FORM constant missing"
    form_match = re.search(
        r"UPLOAD_FORM\[\]\s*PROGMEM\s*=\s*((?:\"[^\"]*\"\s*)+);",
        ino_source,
    )
    assert form_match, "could not locate UPLOAD_FORM string literal"
    form_html = form_match.group(1)
    assert "method='POST'" in form_html
    assert "action='/update'" in form_html
    assert "enctype='multipart/form-data'" in form_html
    assert "type='file'" in form_html
