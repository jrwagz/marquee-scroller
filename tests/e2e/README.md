# SPA end-to-end tests (Playwright)

Browser-driven smoke + diagnostic scripts for the Preact SPA at `/spa/`.
Intended for fast iteration on UI bugs against a live device — not part
of CI (no live device in CI).

## Setup

One-time, host-side venv (≈100 MB for Chromium):

```bash
python3 -m venv tests/e2e/.venv
tests/e2e/.venv/bin/pip install -r tests/e2e/requirements.txt
tests/e2e/.venv/bin/playwright install chromium
```

Or via the `make` target which wraps the venv lifecycle:

```bash
make test-e2e HOST=192.168.8.161
```

## Scripts

| Script | What it does |
| --- | --- |
| `scripts/smoke.py` | Walks Status → Settings → Actions; captures console errors, page errors, HTTP 4xx/5xx; screenshots each tab. Returns non-zero if anything is flagged. |
| `scripts/diagnose_settings.py` | Deep-dive on the Settings tab — tab-state DOM eval, network log, form-section presence check. Pattern to copy when investigating a specific tab. |

Each script accepts an optional URL argument; default is
`http://192.168.8.161/spa/`.

```bash
tests/e2e/.venv/bin/python tests/e2e/scripts/smoke.py
tests/e2e/.venv/bin/python tests/e2e/scripts/smoke.py http://other-clock/spa/
```

Screenshots and outputs land in `tests/e2e/out/` (gitignored).

## Iteration loop

Describe a UI bug → write or adapt a script under `scripts/` that
reproduces it → run → read screenshots and console/network logs → fix
the SPA → re-run.

## Gotchas

**Don't trust `wait_for_load_state("networkidle")` for SPA tests.** It
fires when the network goes quiet for 500ms, which can land in the gap
between an `/api/*` response arriving and Preact re-rendering with the
new state. The result: a screenshot of the loading placeholder with
all the post-fetch DOM still missing. Always wait for a specific
content selector instead — e.g. `page.locator(".form-section").first
.wait_for(state="visible")` for the Settings tab.

**Screenshots can leak credentials.** The Settings tab renders the
OpenWeatherMap and Calendar API keys in plaintext input fields. Any
full-page screenshot captures them. Before sharing screenshots in PRs,
issues, or external chat, redact the secrets — either by overlaying a
black rect via Playwright's `page.evaluate(() => { ... })` to clear the
input values before screenshot, or by post-processing the PNG to mask
the relevant regions. The `redact` helper in `scripts/_helpers.py`
clears known-sensitive input values for diagnostic-screenshot use.
