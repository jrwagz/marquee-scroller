"""
Capture full DOM + screenshots of every legacy UI page so we can verify
feature-parity in the SPA.

Output:
  tests/e2e/out/legacy/{home,configure,update,updatefs,updateFromUrl}.png
  tests/e2e/out/legacy/{home,configure}.html (saved DOM)
  tests/e2e/out/legacy/audit.txt            (catalog of inputs / links / data)

Skips destructive routes (/systemreset, /forgetwifi, /pull) — those
side-effect the device.
"""

import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.8.161"
OUT = Path(__file__).resolve().parent.parent / "out" / "legacy"
OUT.mkdir(parents=True, exist_ok=True)


def audit_form(page, label: str) -> list[str]:
    """Return descriptions of every form input on the page."""
    inputs = page.evaluate(
        """() => Array.from(document.querySelectorAll('input, select, textarea, button')).map(el => ({
            tag: el.tagName.toLowerCase(),
            type: el.type || '',
            name: el.name || '',
            id: el.id || '',
            value: (el.tagName === 'BUTTON' ? el.textContent : el.value) || '',
            checked: el.checked || false,
            label: (el.labels && el.labels[0]) ? el.labels[0].textContent.trim() : '',
        }))"""
    )
    lines = [f"  [{label}] {len(inputs)} form elements:"]
    for inp in inputs:
        line = f"    {inp['tag']}"
        if inp["type"]:
            line += f"[{inp['type']}]"
        if inp["name"]:
            line += f" name={inp['name']}"
        if inp["id"]:
            line += f" id={inp['id']}"
        if inp["label"]:
            line += f"  label={inp['label']!r}"
        if inp["value"]:
            v = inp["value"][:40] + ("…" if len(inp["value"]) > 40 else "")
            line += f"  value={v!r}"
        if inp["type"] == "checkbox":
            line += f"  checked={inp['checked']}"
        lines.append(line)
    return lines


def audit_links(page, label: str) -> list[str]:
    """Return every <a href> on the page."""
    links = page.evaluate(
        """() => Array.from(document.querySelectorAll('a[href]')).map(a => ({
            href: a.getAttribute('href'),
            text: a.textContent.trim().replace(/\\s+/g, ' '),
        }))"""
    )
    return [f"  [{label}] link: {a['href']}  text={a['text']!r}" for a in links]


def audit_text(page, label: str) -> list[str]:
    """Return all visible text in <main> / <body> for content discovery."""
    text = page.evaluate(
        """() => document.body.innerText.replace(/\\s+/g, ' ').trim()"""
    )
    return [f"  [{label}] body-text: {text}"]


def main() -> int:
    pages_to_audit = [
        ("home", f"{BASE}/", "h2", True),
        ("configure", f"{BASE}/configure", "h2", True),
        ("update", f"{BASE}/update", "h2", True),
        ("updatefs", f"{BASE}/updatefs", "h2", True),
        ("updateFromUrl", f"{BASE}/updateFromUrl?firmwareUrl=", "h2", False),  # don't trigger
    ]

    audit_lines: list[str] = ["=== legacy UI audit ===", f"target: {BASE}", ""]

    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 1024, "height": 800})
        page = context.new_page()
        page.on(
            "console",
            lambda m: audit_lines.append(f"  console[{m.type}]: {m.text}")
            if m.type in ("error", "warning")
            else None,
        )

        for label, url, wait_for, do_audit in pages_to_audit:
            audit_lines.append(f"--- {label}: GET {url} ---")
            try:
                page.goto(url, timeout=12000, wait_until="domcontentloaded")
                if wait_for:
                    try:
                        page.locator(wait_for).first.wait_for(
                            state="visible", timeout=4000
                        )
                    except Exception:
                        audit_lines.append(f"  (no '{wait_for}' visible after 4s)")
                page.screenshot(path=str(OUT / f"{label}.png"), full_page=True)
                if label in ("home", "configure"):
                    (OUT / f"{label}.html").write_text(page.content(), encoding="utf-8")
                if do_audit:
                    audit_lines += audit_links(page, label)
                    audit_lines += audit_form(page, label)
                    audit_lines += audit_text(page, label)
            except Exception as e:
                audit_lines.append(f"  ERROR: {e}")
            audit_lines.append("")

        browser.close()

    out_path = OUT / "audit.txt"
    out_path.write_text("\n".join(audit_lines), encoding="utf-8")
    print("\n".join(audit_lines))
    print(f"\n→ written to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
