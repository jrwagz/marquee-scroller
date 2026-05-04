"""
Deep-dive on the Settings tab — pattern to copy for tab-specific
investigations.

Captures: every /api/* request + status, every console message, the DOM
state of the tab bar, and explicit waits for content (not networkidle).
Saves a redacted screenshot to tests/e2e/out/settings-diagnostic.png.

Usage:
  python tests/e2e/scripts/diagnose_settings.py [URL]
"""

import sys

from playwright.sync_api import sync_playwright

from _helpers import OUT, redact

URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161/spa/"

console: list[str] = []
requests: list[str] = []


def main() -> int:
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 480, "height": 800})
        page = context.new_page()
        page.on("console", lambda m: console.append(f"[{m.type}] {m.text}"))
        page.on("pageerror", lambda e: console.append(f"[pageerror] {e}"))
        page.on(
            "response",
            lambda r: requests.append(f"  {r.status} {r.request.method} {r.url}"),
        )

        page.goto(URL, timeout=15000)
        page.locator(".stat-grid").wait_for(state="visible", timeout=10000)
        print("=== After initial load ===")
        print(_tab_state(page))

        print("\n=== Click Settings ===")
        before = len(requests)
        page.get_by_role("button", name="Settings").click()
        page.locator(".form-section").first.wait_for(state="visible", timeout=10000)

        print("requests during/after click:")
        for r in requests[before:]:
            print(r)

        print(_tab_state(page))

        body_text = page.locator("main.container").inner_text()
        print(f"main content text:\n{'-' * 50}\n{body_text}\n{'-' * 50}")

        sections = page.evaluate(
            """() => Array.from(document.querySelectorAll('.form-section h2'))
                          .map(h => h.textContent)"""
        )
        print(f"form sections found: {sections}")

        api_config = [r for r in requests if "/api/config" in r]
        print(f"/api/config calls: {api_config}")

        redact(page)
        page.screenshot(
            path=str(OUT / "settings-diagnostic.png"),
            full_page=True,
        )

        print("\n=== Click Actions ===")
        page.get_by_role("button", name="Actions").click()
        page.locator(".action-card").first.wait_for(state="visible", timeout=10000)
        print(_tab_state(page))

        print("\n=== console output ===")
        for c in console:
            print(c)

        browser.close()
    return 0


def _tab_state(page) -> str:
    return "tabs: " + str(
        page.evaluate(
            """() => Array.from(document.querySelectorAll('.tab')).map(b => ({
                label: b.textContent,
                active: b.classList.contains('active')
            }))"""
        )
    )


if __name__ == "__main__":
    sys.exit(main())
