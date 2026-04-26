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


def test_patch_device_name(client):
    # Register via heartbeat
    client.get("/api/v1/calendar?chip_id=dev001")

    resp = client.patch(
        "/api/v1/devices/dev001",
        json={"name": "Living Room"},
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    assert resp.json()["name"] == "Living Room"


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
