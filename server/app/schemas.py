from datetime import datetime

from pydantic import BaseModel


class DeviceResponse(BaseModel):
    id: int
    chip_id: str
    name: str | None
    version: str | None
    uptime_ms: int | None
    free_heap: int | None
    rssi: int | None
    ip_address: str | None
    last_seen: datetime
    created_at: datetime

    model_config = {"from_attributes": True}


class DeviceUpdate(BaseModel):
    name: str | None = None
