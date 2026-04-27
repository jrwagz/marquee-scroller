"""Shared fixtures for integration tests."""

import pytest
import requests
from requests.auth import HTTPBasicAuth


def pytest_addoption(parser):
    parser.addoption("--host", required=True, help="Device IP address")
    parser.addoption("--port", type=int, default=80, help="Device port")
    parser.addoption("--password", default=None, help="Device web password (prompted if not set)")


@pytest.fixture(scope="session")
def device_url(request):
    host = request.config.getoption("--host")
    port = request.config.getoption("--port")
    return f"http://{host}:{port}"


@pytest.fixture(scope="session")
def device_auth(request, device_url):
    """Discover device credentials. Tries default, then --password, then prompts."""
    default = HTTPBasicAuth("admin", "password")
    r = requests.get(f"{device_url}/api/status", auth=default, timeout=10)
    if r.status_code == 200:
        return default

    pw = request.config.getoption("--password")
    if not pw:
        pw = input("Device password (not default 'password'): ").strip()

    auth = HTTPBasicAuth("admin", pw)
    r = requests.get(f"{device_url}/api/status", auth=auth, timeout=10)
    if r.status_code != 200:
        pytest.exit(f"Auth failed with provided password (status {r.status_code})")
    return auth


@pytest.fixture(scope="session")
def default_auth():
    return HTTPBasicAuth("admin", "password")


@pytest.fixture(scope="session")
def csrf_headers():
    return {"X-Requested-With": "test"}
