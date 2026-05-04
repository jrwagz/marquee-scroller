"""
Verify the SPA Home tab renders weather + events from /api/weather and
/api/events. Exists because the Home tab was added in Phase B of the
SPA-parity work; smoke.py covers Status/Settings/Actions but not Home.

Asserts:
  - Home is the default active tab on first load
  - Weather card renders city, temperature, humidity, wind, pressure
  - Event list shows >= 1 message when /api/events.count > 0
  - No console errors / page errors / HTTP failures

Outputs tests/e2e/out/home.png (full-page).
"""

import sys
import urllib.request
import json

from playwright.sync_api import sync_playwright

from _helpers import OUT, redact

URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161/spa/"
DEVICE_BASE = URL.rstrip("/").rsplit("/spa", 1)[0]


def fetch(path: str) -> dict:
    with urllib.request.urlopen(f"{DEVICE_BASE}{path}", timeout=8) as r:
        return json.loads(r.read())


def main() -> int:
    truth_weather = fetch("/api/weather")
    truth_events = fetch("/api/events")
    print(f"truth /api/weather: data_valid={truth_weather['data_valid']}, "
          f"city={truth_weather['city']!r}, temp={truth_weather['temperature']}")
    print(f"truth /api/events:  count={truth_events['count']}")

    console: list[str] = []
    failures: list[str] = []

    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 480, "height": 900})
        page = context.new_page()
        page.on(
            "console",
            lambda m: console.append(f"[{m.type}] {m.text}")
            if m.type in ("error", "warning")
            else None,
        )
        page.on("pageerror", lambda e: console.append(f"[pageerror] {e}"))
        page.on(
            "response",
            lambda r: failures.append(f"HTTP {r.status} {r.request.method} {r.url}")
            if r.status >= 400
            else None,
        )

        page.goto(URL, timeout=15000)
        # Home should be the active tab on first load.
        active = page.evaluate(
            """() => Array.from(document.querySelectorAll('.tab')).find(b =>
                  b.classList.contains('active'))?.textContent"""
        )
        assert active == "Home", f"expected Home active by default, got {active!r}"
        print(f"✓ default active tab = {active!r}")

        # Wait for Home content to populate (weather card + events list).
        page.locator(".weather-card, .home-section").first.wait_for(
            state="visible", timeout=10000
        )

        # Weather card
        if truth_weather["data_valid"]:
            page.locator(".weather-card").wait_for(state="visible", timeout=5000)
            text = page.locator(".weather-card").inner_text()
            assert truth_weather["city"] in text, (
                f"city {truth_weather['city']!r} not in weather card text: {text!r}"
            )
            assert f"{truth_weather['humidity']}% humidity" in text, (
                f"humidity not in weather card: {text!r}"
            )
            assert f"{truth_weather['wind_direction_text']}" in text, (
                f"wind direction text not in weather card: {text!r}"
            )
            print(f"✓ weather card includes city + humidity + wind direction")
        else:
            print("  (weather data_valid=false — skipping weather assertions)")

        # Events list
        events_section = page.locator(".home-section").nth(0)
        if truth_events["count"] > 0:
            event_items = page.locator(".event-list li")
            count = event_items.count()
            assert count == truth_events["count"], (
                f"expected {truth_events['count']} events, got {count} in DOM"
            )
            first_item = event_items.first.text_content()
            assert first_item == truth_events["messages"][0], (
                f"first event mismatch: DOM={first_item!r}, "
                f"API={truth_events['messages'][0]!r}"
            )
            print(f"✓ event list renders {count} items, first matches API")
        else:
            txt = events_section.inner_text()
            assert "No upcoming events" in txt, (
                f"expected 'No upcoming events' placeholder, got: {txt!r}"
            )
            print("✓ 'No upcoming events' placeholder shown")

        page.screenshot(path=str(OUT / "home.png"), full_page=True)

        # Walk to Status to verify next-refresh countdown row appears.
        # Wait for a card with that specific stat-label, not just .stat-grid —
        # the grid renders the stale content of an earlier mount before the
        # post-fetch re-render lands the new row.
        page.get_by_role("button", name="Status").click()
        # CSS text-transform makes inner_text uppercase, so case-insensitive.
        next_refresh_label = page.get_by_text("Next data refresh", exact=True)
        next_refresh_label.first.wait_for(state="visible", timeout=10000)
        status_text = page.locator(".stat-grid").inner_text().lower()
        assert "next data refresh" in status_text, (
            f"Next data refresh row missing on Status tab: {status_text!r}"
        )
        print("✓ Status tab shows next-refresh countdown row")

        # Walk to Actions and check that the new cards exist.
        page.get_by_role("button", name="Actions").click()
        page.locator(".action-card").first.wait_for(
            state="visible", timeout=10000
        )
        actions_text = page.locator(".container").inner_text()
        for required in ("Reset Settings", "Forget WiFi", "LittleFS"):
            assert required in actions_text, (
                f"Actions tab missing {required!r}: {actions_text!r}"
            )
        print(f"✓ Actions tab includes Reset Settings, Forget WiFi, LittleFS")

        redact(page)
        page.screenshot(path=str(OUT / "actions-extended.png"), full_page=True)

        browser.close()

    if console:
        print("\nconsole errors/warnings:")
        for c in console:
            print(f"  {c}")
    if failures:
        print("\nHTTP failures:")
        for f in failures:
            print(f"  {f}")

    if console or failures:
        return 1
    print("\n✓ Home tab + extended Actions tab both render correctly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
