"""
SPA smoke test: load /spa/, walk Status → Settings → Actions, capture
console errors, page errors, network failures, and one screenshot per
tab. Intended for fast iteration on UI bugs against a live device.

Each tab waits for a known DOM signal (not networkidle) before
screenshotting — networkidle fires when the network is quiet, which
can land BEFORE Preact re-renders post-fetch, producing misleading
"Loading…" screenshots.

Usage:
  python tests/e2e/scripts/smoke.py [URL]

Default URL: http://192.168.8.161/spa/
Outputs to tests/e2e/out/{status,settings,actions}.png

Exit code: 0 if no errors detected, 1 if console/page/HTTP errors found.
"""

import sys

from playwright.sync_api import ConsoleMessage, Request, sync_playwright

from _helpers import OUT, redact

URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161/spa/"

console_msgs: list[str] = []
page_errors: list[str] = []
failed_requests: list[str] = []


def on_console(msg: ConsoleMessage) -> None:
    if msg.type in ("error", "warning"):
        console_msgs.append(f"[{msg.type}] {msg.text}")


def on_pageerror(err: Exception) -> None:
    page_errors.append(str(err))


def on_requestfailed(req: Request) -> None:
    failed_requests.append(f"{req.method} {req.url} — {req.failure}")


def on_response(res) -> None:
    if res.status >= 400:
        failed_requests.append(f"HTTP {res.status} {res.request.method} {res.url}")


# Per-tab DOM signal that, once visible, means the tab content has
# rendered with real data (not the loading placeholder).
TABS = [
    ("Status", ".stat-grid"),
    ("Settings", ".form-section"),
    ("Actions", ".action-card"),
]


def main() -> int:
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 480, "height": 800})
        page = context.new_page()
        page.on("console", on_console)
        page.on("pageerror", on_pageerror)
        page.on("requestfailed", on_requestfailed)
        page.on("response", on_response)

        print(f"→ navigating to {URL}")
        page.goto(URL, timeout=15000)

        for label, selector in TABS:
            print(f"→ clicking {label}, waiting for {selector}")
            page.get_by_role("button", name=label).click()
            page.locator(selector).first.wait_for(state="visible", timeout=10000)
            if label == "Settings":
                redact(page)
            page.screenshot(path=str(OUT / f"{label.lower()}.png"), full_page=True)

        title = page.title()
        h1 = page.locator("h1").first.text_content()
        browser.close()

    print()
    print(f"page title: {title!r}")
    print(f"page h1:    {h1!r}")
    print()

    if console_msgs:
        print("CONSOLE MESSAGES (errors/warnings):")
        for m in console_msgs:
            print(f"  {m}")
        print()
    if page_errors:
        print("PAGE ERRORS (uncaught JS):")
        for e in page_errors:
            print(f"  {e}")
        print()
    if failed_requests:
        print("FAILED / 4xx / 5xx REQUESTS:")
        for r in failed_requests:
            print(f"  {r}")
        print()

    if not (console_msgs or page_errors or failed_requests):
        print("✓ no console errors, page errors, or HTTP failures detected")
    else:
        print("✗ issues above")

    print(f"\nscreenshots in {OUT}/")
    return 1 if (console_msgs or page_errors or failed_requests) else 0


if __name__ == "__main__":
    sys.exit(main())
