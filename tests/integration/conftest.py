"""Shared fixtures for integration tests."""

import pytest
import requests


def pytest_addoption(parser):
    parser.addoption("--host", required=True, help="Device IP address")
    parser.addoption("--port", type=int, default=80, help="Device port")


@pytest.fixture(scope="session")
def device_url(request):
    host = request.config.getoption("--host")
    port = request.config.getoption("--port")
    return f"http://{host}:{port}"
