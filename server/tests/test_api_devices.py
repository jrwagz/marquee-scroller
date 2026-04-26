"""Integration tests for the devices API endpoints."""


def test_list_devices_requires_auth(client):
    resp = client.get("/api/v1/devices")
    assert resp.status_code == 401


def test_list_devices_with_auth(client):
    resp = client.get(
        "/api/v1/devices",
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    assert resp.json() == []


def test_get_device_not_found(client):
    resp = client.get(
        "/api/v1/devices/nonexistent",
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 404


def test_update_device_name(client):
    # Register via heartbeat
    client.get("/api/v1/calendar?chip_id=dev001")

    resp = client.post(
        "/api/v1/devices/dev001/update_name",
        json={"name": "Living Room"},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    assert resp.json()["name"] == "Living Room"


def test_update_device_name_requires_auth(client):
    client.get("/api/v1/calendar?chip_id=dev001")
    resp = client.post(
        "/api/v1/devices/dev001/update_name",
        json={"name": "x"},
    )
    assert resp.status_code == 401


def test_update_device_name_404_when_unknown_device(client):
    resp = client.post(
        "/api/v1/devices/never-seen/update_name",
        json={"name": "x"},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 404


def test_update_device_name_rejects_missing_name(client):
    """Name is required (no other fields are accepted)."""
    client.get("/api/v1/calendar?chip_id=dev001")
    resp = client.post(
        "/api/v1/devices/dev001/update_name",
        json={},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 422  # FastAPI validation error


def test_update_device_name_ignores_other_fields(client):
    """Sending unrelated fields like version/uptime must NOT mutate them."""
    client.get(
        "/api/v1/calendar?chip_id=dev001&version=3.08.0&uptime=1000&heap=30000&rssi=-60"
    )
    resp = client.post(
        "/api/v1/devices/dev001/update_name",
        json={"name": "Lab", "version": "9.9.9", "uptime_ms": 99, "rssi": 0},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    body = resp.json()
    assert body["name"] == "Lab"
    # Telemetry fields untouched (extra body fields silently ignored by Pydantic)
    assert body["version"] == "3.08.0"
    assert body["uptime_ms"] == 1000
    assert body["rssi"] == -60


def test_old_patch_endpoint_returns_method_not_allowed(client):
    """The previous PATCH /api/v1/devices/{chip_id} surface is gone (405)."""
    client.get("/api/v1/calendar?chip_id=dev001")
    resp = client.patch(
        "/api/v1/devices/dev001",
        json={"name": "x"},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 405


def test_get_device_after_heartbeat(client):
    client.get("/api/v1/calendar?chip_id=dev002&version=3.09.0&heap=32000&rssi=-58")

    resp = client.get(
        "/api/v1/devices/dev002",
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    device = resp.json()
    assert device["chip_id"] == "dev002"
    assert device["version"] == "3.09.0"
    assert device["free_heap"] == 32000
    assert device["rssi"] == -58


def test_bearer_auth_also_works(client):
    resp = client.get(
        "/api/v1/devices",
        headers={"Authorization": "Bearer test-key"},
    )
    assert resp.status_code == 200
