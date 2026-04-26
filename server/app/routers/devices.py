from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.auth import require_auth
from app.database import get_db
from app.models import Device
from app.schemas import DeviceResponse, DeviceUpdate

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


@router.patch("/api/v1/devices/{chip_id}", response_model=DeviceResponse)
def update_device(chip_id: str, body: DeviceUpdate, db: Session = Depends(get_db)):
    device = db.query(Device).filter(Device.chip_id == chip_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    if body.name is not None:
        device.name = body.name
    db.commit()
    db.refresh(device)
    return device
