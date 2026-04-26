from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.auth import require_auth
from app.database import get_db
from app.models import Device
from app.schemas import DeviceNameUpdate, DeviceResponse

router = APIRouter(dependencies=[Depends(require_auth)])


@router.get("/api/v1/devices", response_model=list[DeviceResponse])
def list_devices(db: Session = Depends(get_db)):
    return db.query(Device).order_by(Device.last_seen.desc()).all()


@router.get("/api/v1/devices/{chip_id}", response_model=DeviceResponse)
def get_device(chip_id: str, db: Session = Depends(get_db)):
    device = db.query(Device).filter(Device.chip_id == chip_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


@router.post(
    "/api/v1/devices/{chip_id}/update_name",
    response_model=DeviceResponse,
    summary="Set the human-friendly display name for a device",
    description=(
        "Updates ONLY the device's `name` field — no other device fields can be "
        "modified through this endpoint. The name is what gets pushed back to the "
        "clock as `deviceName` in the next calendar response. Telemetry fields "
        "(`version`, `uptime_ms`, `free_heap`, `rssi`, `ip_address`, `last_seen`) "
        "are owned by the heartbeat ingestion path and are not user-editable."
    ),
)
def update_device_name(
    chip_id: str,
    body: DeviceNameUpdate,
    db: Session = Depends(get_db),
):
    device = db.query(Device).filter(Device.chip_id == chip_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    device.name = body.name
    db.commit()
    db.refresh(device)
    return device
