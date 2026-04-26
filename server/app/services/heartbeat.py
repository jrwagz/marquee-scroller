from datetime import datetime, timezone

from sqlalchemy.orm import Session

from app.models import Device


def record_heartbeat(
    db: Session,
    chip_id: str,
    version: str | None = None,
    uptime_ms: int | None = None,
    free_heap: int | None = None,
    rssi: int | None = None,
    ip_address: str | None = None,
) -> Device:
    device = db.query(Device).filter(Device.chip_id == chip_id).first()
    now = datetime.now(timezone.utc)

    if device is None:
        device = Device(chip_id=chip_id, created_at=now)
        db.add(device)

    device.last_seen = now
    if version is not None:
        device.version = version
    if uptime_ms is not None:
        device.uptime_ms = uptime_ms
    if free_heap is not None:
        device.free_heap = free_heap
    if rssi is not None:
        device.rssi = rssi
    if ip_address is not None:
        device.ip_address = ip_address

    db.commit()
    db.refresh(device)
    return device
