"""Integration tests for the calendar API endpoint."""


def test_calendar_returns_json(client):
    resp = client.get("/api/v1/calendar")
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, list)
    assert "config" in data[0]
    assert any("message" in item for item in data)


def test_calendar_with_heartbeat_registers_device(client):
    resp = client.get("/api/v1/calendar?chip_id=test123&version=3.08.0&rssi=-62")
    assert resp.status_code == 200

    # Verify device was registered
    resp = client.get(
        "/api/v1/devices",
        headers={"Authorization": "token test-key"},
    )
    assert resp.status_code == 200
    devices = resp.json()
    assert len(devices) == 1
    assert devices[0]["chip_id"] == "test123"
    assert devices[0]["version"] == "3.08.0"


def test_calendar_without_chip_id_still_works(client):
    resp = client.get("/api/v1/calendar")
    assert resp.status_code == 200
    data = resp.json()
    assert data[0]["config"]["eventToday"] in (0, 1)


def test_calendar_device_name_in_config(client):
    # Register device with heartbeat
    client.get("/api/v1/calendar?chip_id=named123")

    # Set device name
    client.post(
        "/api/v1/devices/named123/update_name",
        json={"name": "Kitchen Clock"},
        headers={"Authorization": "token test-key"},
    )

    # Now calendar should include deviceName
    resp = client.get("/api/v1/calendar?chip_id=named123")
    data = resp.json()
    assert data[0]["config"]["deviceName"] == "Kitchen Clock"
