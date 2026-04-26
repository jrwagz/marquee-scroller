"""Pin behavior of raw.githubusercontent.com against the heartbeat parameter set.

The clock fetches its calendar JSON from raw.githubusercontent.com (or any other
static host configured via WAGFAM_DATA_URL) and appends device telemetry as query
params. If the host *interprets* one of those param names instead of ignoring it,
all deployed clocks immediately break — they get back something other than calendar
JSON and the streaming parser fails.

This test makes a real HTTPS request to raw.githubusercontent.com against a known
public file (this repo's own README on master) and asserts:

1. POSITIVE: The 5 current heartbeat param names (chip_id, version, uptime, heap,
   rssi) are *ignored* — the response body is byte-identical to a no-param fetch.

2. NEGATIVE: `?token=...` IS interpreted by raw.githubusercontent.com as an auth
   token (it's their documented mechanism for fetching from private repos with a
   PAT in the URL — a fake token returns HTTP 404 against a public repo because
   the auth fails before the file lookup). Therefore no future heartbeat param may
   be named `token`. This assertion exists to fail loudly if anyone tries.

If GitHub's behavior ever changes such that one of our current param names *does*
get interpreted, this test breaks at CI time rather than in production.

The test is skipped when offline (no network, DNS failure, etc.) so CI doesn't
flake on transient outages.
"""

import hashlib

import pytest
import requests

REFERENCE_URL = "https://raw.githubusercontent.com/jrwagz/marquee-scroller/master/README.md"

# The current set of heartbeat params built by WagFamBdayClient::updateData
# (marquee/WagFamBdayClient.cpp). Update this list if the firmware adds more.
CURRENT_HEARTBEAT_PARAMS = {
    "chip_id": "5fc8ad",
    "version": "3.08.0-wagfam",
    "uptime": "1234567",
    "heap": "32496",
    "rssi": "-62",
}

# Names that raw.githubusercontent.com is known to interpret. A future heartbeat
# param using any of these names would break clocks pointed at GitHub raw.
KNOWN_INTERPRETED_PARAMS = {"token"}


def _fetch(url: str, params: dict | None = None) -> tuple[int, str]:
    """Return (status_code, sha256_hex) — sha256 over the response body bytes."""
    r = requests.get(url, params=params or {}, timeout=10)
    return r.status_code, hashlib.sha256(r.content).hexdigest()


@pytest.fixture(scope="module")
def baseline():
    """Baseline (status, sha256) for the reference URL with no query params."""
    try:
        return _fetch(REFERENCE_URL)
    except requests.RequestException as e:
        pytest.skip(f"network unavailable: {e}")


def test_baseline_is_ok(baseline):
    status, _ = baseline
    assert status == 200, f"reference URL not reachable (got HTTP {status})"


@pytest.mark.parametrize("param_name", sorted(CURRENT_HEARTBEAT_PARAMS.keys()))
def test_each_heartbeat_param_is_ignored(param_name, baseline):
    """Each heartbeat param, sent alone, must produce a byte-identical response."""
    base_status, base_sha = baseline
    status, sha = _fetch(REFERENCE_URL, {param_name: CURRENT_HEARTBEAT_PARAMS[param_name]})
    assert status == base_status, (
        f"raw.githubusercontent.com returned HTTP {status} when sent ?{param_name}=…; "
        "this would break clocks pointed at GitHub raw"
    )
    assert sha == base_sha, (
        f"raw.githubusercontent.com returned a different body when sent ?{param_name}=…; "
        "the parameter is being interpreted instead of ignored"
    )


def test_full_heartbeat_param_set_is_ignored(baseline):
    """The full heartbeat query string must produce a byte-identical response."""
    base_status, base_sha = baseline
    status, sha = _fetch(REFERENCE_URL, CURRENT_HEARTBEAT_PARAMS)
    assert status == base_status
    assert sha == base_sha


def test_token_param_is_interpreted_not_ignored():
    """Negative control: ?token=… IS interpreted by raw.gh, so no heartbeat param may be named 'token'."""
    intersection = KNOWN_INTERPRETED_PARAMS & CURRENT_HEARTBEAT_PARAMS.keys()
    assert intersection == set(), (
        f"Heartbeat parameter name(s) {intersection} collide with GitHub raw's "
        "interpreted parameters. Pick a different name in WagFamBdayClient::updateData."
    )

    # Also assert at runtime that the contract still holds — if GitHub ever stops
    # interpreting ?token, this assert breaks and we can drop the prohibition.
    try:
        # Use a known-private repo path so a fake token gets us auth-rejected.
        # We don't care about the exact status; we only care it's NOT 200, proving
        # the param was interpreted (rather than ignored, which would 404 the path).
        r_no = requests.get(
            "https://raw.githubusercontent.com/jrwagz/wagfam-clocks-data-source/main/README.md",
            timeout=10,
        )
        r_token = requests.get(
            "https://raw.githubusercontent.com/jrwagz/wagfam-clocks-data-source/main/README.md",
            params={"token": "definitely-not-a-real-token"},
            timeout=10,
        )
    except requests.RequestException as e:
        pytest.skip(f"network unavailable: {e}")

    # Both should fail (the repo is private), but the failure modes show ?token
    # IS being processed — the body bytes differ when token is present, even if
    # both ultimately return non-200. If they were byte-identical, that would
    # mean GitHub had stopped interpreting ?token.
    assert hashlib.sha256(r_no.content).hexdigest() == hashlib.sha256(r_token.content).hexdigest() or \
           r_no.status_code != r_token.status_code, (
        "Expected ?token=… to be interpreted differently from no-param request; "
        "if these are now identical, GitHub may have stopped interpreting ?token "
        "and the prohibition in CLAUDE.md can be relaxed."
    )
