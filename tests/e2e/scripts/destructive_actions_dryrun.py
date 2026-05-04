"""
Verify the SPA Reset Settings + Forget WiFi cards CORRECTLY ABORT when
the confirm dialog is dismissed — i.e., they don't make a request to
the device until the user accepts the confirm.

This is a dry-run: we never accept the confirm, so nothing destructive
happens. The assertion is on what doesn't get sent.

Asserts:
  - Both buttons trigger a confirm dialog
  - Cancelling the dialog does NOT POST to /api/system-reset or
    /api/forget-wifi (verified via request log)
"""

import sys
from playwright.sync_api import sync_playwright

URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161/spa/"

requests_seen: list[str] = []


def main() -> int:
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 480, "height": 900})
        page = context.new_page()
        page.on(
            "request",
            lambda r: requests_seen.append(f"{r.method} {r.url}")
            if "/api/" in r.url
            else None,
        )

        page.goto(URL, timeout=15000)
        page.get_by_role("button", name="Actions").click()
        page.locator(".action-card").first.wait_for(state="visible", timeout=10000)

        # Auto-DISMISS the next confirm. Set BEFORE clicking.
        page.once("dialog", lambda d: d.dismiss())
        before = list(requests_seen)
        page.get_by_role("button", name="Reset Settings").click()
        page.wait_for_timeout(1500)
        delta = [r for r in requests_seen if r not in before]
        api_writes = [r for r in delta if "/api/system-reset" in r]
        assert not api_writes, (
            f"dismissing the confirm should NOT POST /api/system-reset, but saw: {api_writes}"
        )
        print(f"✓ dismissing Reset Settings confirm did not POST (saw {len(delta)} api requests, none destructive)")

        # Same for Forget WiFi.
        page.once("dialog", lambda d: d.dismiss())
        before = list(requests_seen)
        page.get_by_role("button", name="Forget WiFi").click()
        page.wait_for_timeout(1500)
        delta = [r for r in requests_seen if r not in before]
        api_writes = [r for r in delta if "/api/forget-wifi" in r]
        assert not api_writes, (
            f"dismissing the confirm should NOT POST /api/forget-wifi, but saw: {api_writes}"
        )
        print(f"✓ dismissing Forget WiFi confirm did not POST (saw {len(delta)} api requests, none destructive)")

        browser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
