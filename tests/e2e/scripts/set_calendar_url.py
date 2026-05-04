"""
End-to-end test: set the Calendar JSON URL via the SPA Settings form,
click Save, verify persistence via /api/config.

Exercises the full UI flow: tab switch → input edit → save-bar state
transitions (idle → saving → ok) → POST /api/config → server roundtrip.

Usage:
  python tests/e2e/scripts/set_calendar_url.py [URL] [VALUE]

Defaults:
  URL   = http://192.168.8.161/spa/
  VALUE = https://wagfam-server.azurewebsites.net/api/v1/calendar
"""

import json
import sys
import urllib.request

from playwright.sync_api import sync_playwright

from _helpers import OUT, redact

SPA_URL = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161/spa/"
CAL_URL = (
    sys.argv[2]
    if len(sys.argv) > 2
    else "https://wagfam-server.azurewebsites.net/api/v1/calendar"
)
DEVICE_BASE = SPA_URL.rstrip("/").rsplit("/spa", 1)[0]


def get_config_field(field: str) -> str:
    """Fetch a single field from /api/config server-side (out-of-band
    from the browser, so we know what's actually persisted)."""
    with urllib.request.urlopen(f"{DEVICE_BASE}/api/config", timeout=8) as r:
        cfg = json.loads(r.read())
    return cfg.get(field, "<missing>")


def main() -> int:
    print(f"target: {SPA_URL}")
    print(f"value:  {CAL_URL}")

    before = get_config_field("wagfam_data_url")
    print(f"\n[before] wagfam_data_url = {before!r}")

    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 480, "height": 800})
        page = context.new_page()
        console: list[str] = []
        failures: list[str] = []
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

        # Load + go to Settings.
        page.goto(SPA_URL, timeout=15000)
        page.get_by_role("button", name="Settings").click()
        page.locator(".form-section").first.wait_for(state="visible", timeout=10000)
        print("\n→ settings tab loaded")

        # Find the Calendar JSON URL input. ID matches `id="wagfam-url"` from
        # SettingsPage.tsx's TextRow.
        cal_input = page.locator("#wagfam-url")
        cal_input.wait_for(state="visible", timeout=5000)
        existing = cal_input.input_value()
        print(f"  current value in form: {existing!r}")

        # Save button starts disabled (no draft). Confirm.
        save_btn = page.get_by_role("button", name="Save")
        assert save_btn.is_disabled(), "Save button should be disabled before any edits"
        print("  ✓ Save button correctly disabled before edits")

        # Fill the field.
        cal_input.fill(CAL_URL)
        print(f"\n→ filled #wagfam-url with target value")

        # Save button should become enabled after the edit.
        page.wait_for_function(
            "document.querySelector('button.btn:not(.btn-danger)').disabled === false",
            timeout=5000,
        )
        print("  ✓ Save button enabled after edit")

        # Look for the unsaved-changes hint.
        unsaved = page.locator(".save-bar .muted").last.text_content()
        print(f"  unsaved hint: {unsaved!r}")

        # Click Save and watch for the success state.
        save_btn.click()
        print("\n→ clicked Save")
        # SettingsPage shows .save-ok ("Saved!") when the POST returns 200.
        page.locator(".save-ok").wait_for(state="visible", timeout=10000)
        ok_text = page.locator(".save-ok").text_content()
        print(f"  ✓ {ok_text!r} appeared in save bar")

        # Field should still show the new value, save button should disable again.
        post_save_value = cal_input.input_value()
        print(f"  field after save: {post_save_value!r}")
        post_save_disabled = save_btn.is_disabled()
        print(f"  save button disabled again: {post_save_disabled}")

        redact(page)
        page.screenshot(path=str(OUT / "set-calendar-url.png"), full_page=True)
        browser.close()

    after = get_config_field("wagfam_data_url")
    print(f"\n[after]  wagfam_data_url = {after!r}")

    if console:
        print("\nconsole errors/warnings:")
        for c in console:
            print(f"  {c}")
    if failures:
        print("\nHTTP failures:")
        for f in failures:
            print(f"  {f}")

    # Verdict.
    if after == CAL_URL:
        print(f"\n✓ device persists wagfam_data_url = {after!r}")
        return 0
    print(f"\n✗ persistence mismatch — server reports {after!r}, expected {CAL_URL!r}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
