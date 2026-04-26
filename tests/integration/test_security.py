"""
Security integration tests for WagFam CalClock REST API.

Run against a live device:
    pytest tests/integration/ --host 192.168.1.100
    pytest tests/integration/ --host 192.168.1.100 --password mypass

Or via make:
    make test-integration HOST=192.168.1.100

Tests each finding from docs/SECURITY_AUDIT.md programmatically.
Requires: requests, pytest (pip install requests pytest)
"""

import pytest
import requests


# ── SEC-01: Firmware upload auth ─────────────────────────────────────────────

class TestSec01FirmwareUploadAuth:
    def test_update_requires_auth(self, device_url):
        r = requests.get(f"{device_url}/update", timeout=10)
        assert r.status_code == 401


# ── SEC-04: Web UI auth ─────────────────────────────────────────────────────

class TestSec04WebUiAuth:
    @pytest.mark.parametrize("path", ["/", "/configure", "/pull", "/systemreset", "/forgetwifi"])
    def test_route_requires_auth(self, device_url, path):
        r = requests.get(f"{device_url}{path}", timeout=10, allow_redirects=False)
        assert r.status_code == 401


# ── SEC-05: Configurable password ────────────────────────────────────────────

class TestSec05ConfigurablePassword:
    def test_default_credentials_rejected(self, device_url, default_auth):
        r = requests.get(f"{device_url}/api/status", auth=default_auth, timeout=10)
        if r.status_code == 200:
            # Default creds work — check if password is still literally "password"
            r2 = requests.get(f"{device_url}/api/config", auth=default_auth, timeout=10)
            assert r2.status_code == 200
            assert r2.json().get("web_password", "") != "password", \
                "web_password is still the hardcoded default 'password'"

    def test_web_password_field_exists(self, device_url, device_auth):
        r = requests.get(f"{device_url}/api/config", auth=device_auth, timeout=10)
        assert r.status_code == 200
        assert "web_password" in r.json()


# ── SEC-06: Protected filesystem paths ───────────────────────────────────────

class TestSec06ProtectedPaths:
    @pytest.mark.parametrize("path", ["/conf.txt", "/ota_pending.txt"])
    def test_write_blocked(self, device_url, device_auth, csrf_headers, path):
        r = requests.post(
            f"{device_url}/api/fs/write",
            auth=device_auth,
            headers=csrf_headers,
            json={"path": path, "content": "test"},
            timeout=10,
        )
        assert r.status_code == 403

    @pytest.mark.parametrize("path", ["/conf.txt", "/ota_pending.txt"])
    def test_delete_blocked(self, device_url, device_auth, csrf_headers, path):
        r = requests.delete(
            f"{device_url}/api/fs/delete?path={path}",
            auth=device_auth,
            headers=csrf_headers,
            timeout=10,
        )
        assert r.status_code in (403, 404)


# ── SEC-07: OWM uses HTTPS (source code check) ──────────────────────────────

class TestSec07OwmHttps:
    def test_owm_connects_on_port_443(self):
        with open("marquee/OpenWeatherMapClient.cpp") as f:
            src = f.read()
        assert "connect(servername, 443)" in src, "still using port 80"

    def test_owm_uses_wifi_client_secure(self):
        with open("marquee/OpenWeatherMapClient.cpp") as f:
            src = f.read()
        assert "WiFiClientSecure" in src, "still using plain WiFiClient"


# ── SEC-09: Form uses POST (source code check) ──────────────────────────────

class TestSec09FormMethod:
    def test_config_form_uses_post(self):
        with open("marquee/marquee.ino") as f:
            src = f.read()
        assert "method='get'><h2>Configure" not in src, "form still uses GET"
        assert "method='post'><h2>Configure" in src, "form POST not found"


# ── SEC-10: CSRF protection ─────────────────────────────────────────────────

class TestSec10Csrf:
    def test_post_without_csrf_header_rejected(self, device_url, device_auth):
        r = requests.post(
            f"{device_url}/api/config",
            auth=device_auth,
            json={"show_date": False},
            timeout=10,
        )
        assert r.status_code == 403

    def test_post_with_csrf_header_accepted(self, device_url, device_auth, csrf_headers):
        r = requests.post(
            f"{device_url}/api/config",
            auth=device_auth,
            headers=csrf_headers,
            json={"show_date": False},
            timeout=10,
        )
        assert r.status_code == 200


# ── SEC-11: Serial output redaction (source code check) ─────────────────────

class TestSec11SerialRedaction:
    def test_owm_api_key_not_logged(self):
        with open("marquee/OpenWeatherMapClient.cpp") as f:
            src = f.read()
        assert "Serial.println(apiGetData)" not in src

    def test_calendar_url_not_logged(self):
        with open("marquee/WagFamBdayClient.cpp") as f:
            src = f.read()
        assert "Serial.println(myJsonSourceUrl)" not in src


# ── SEC-12: Server-push config validation (source code check) ───────────────

class TestSec12ConfigValidation:
    def test_data_source_url_validated_for_https(self):
        with open("marquee/marquee.ino") as f:
            src = f.read()
        assert 'startsWith("https://")' in src, "no HTTPS check on dataSourceUrl"
        assert "Rejected non-HTTPS" in src, "no rejection message"

    def test_firmware_url_domain_validated(self):
        with open("marquee/marquee.ino") as f:
            src = f.read()
        assert "isTrustedFirmwareDomain" in src, "no domain validation"


# ── SEC-14: getMessage bounds check ──────────────────────────────────────────

class TestSec14BoundsCheck:
    def test_bounds_check_in_source(self):
        """Verified by native unit tests; this confirms the guard exists."""
        with open("marquee/WagFamBdayClient.cpp") as f:
            src = f.read()
        assert "index < 0 || index >= messageCounter" in src


# ── SEC-16: Rate limiting ───────────────────────────────────────────────────

class TestSec16RateLimiting:
    @pytest.mark.skip(reason="skipped to avoid rebooting device during test run")
    def test_restart_rate_limited(self, device_url, device_auth, csrf_headers):
        pass
