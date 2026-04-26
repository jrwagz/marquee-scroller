"""Tests for heartbeat service."""

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.database import Base
from app.services.heartbeat import record_heartbeat


def make_db():
    engine = create_engine("sqlite:///:memory:")
    Base.metadata.create_all(bind=engine)
    return sessionmaker(bind=engine)()


def test_first_heartbeat_creates_device():
    db = make_db()
    device = record_heartbeat(db, chip_id="abc123", version="3.08.0", rssi=-62)
    assert device.chip_id == "abc123"
    assert device.version == "3.08.0"
    assert device.rssi == -62
    assert device.name is None
    assert device.created_at is not None


def test_second_heartbeat_updates_device():
    db = make_db()
    record_heartbeat(db, chip_id="abc123", version="3.08.0", rssi=-62)
    device = record_heartbeat(db, chip_id="abc123", version="3.09.0", rssi=-55, free_heap=30000)
    assert device.version == "3.09.0"
    assert device.rssi == -55
    assert device.free_heap == 30000


def test_multiple_devices():
    db = make_db()
    record_heartbeat(db, chip_id="aaa")
    record_heartbeat(db, chip_id="bbb")
    from app.models import Device
    assert db.query(Device).count() == 2


def test_partial_update_preserves_fields():
    db = make_db()
    record_heartbeat(db, chip_id="abc", version="1.0", rssi=-60, ip_address="10.0.0.1")
    device = record_heartbeat(db, chip_id="abc", version="1.1")
    assert device.version == "1.1"
    assert device.rssi == -60
    assert device.ip_address == "10.0.0.1"
