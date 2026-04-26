from fastapi import APIRouter, Depends, Request, Query
from sqlalchemy.orm import Session

from app.config import settings
from app.database import get_db
from app.services.calendar_gen import build_calendar_response, generate_messages, load_calendar_data
from app.services.heartbeat import record_heartbeat

router = APIRouter()


@router.get("/api/v1/calendar")
def get_calendar(
    request: Request,
    chip_id: str | None = Query(None),
    version: str | None = Query(None),
    uptime: int | None = Query(None),
    heap: int | None = Query(None),
    rssi: int | None = Query(None),
    db: Session = Depends(get_db),
):
    device_name = None

    if chip_id:
        ip = request.headers.get("X-Forwarded-For", request.client.host if request.client else None)
        device = record_heartbeat(
            db,
            chip_id=chip_id,
            version=version,
            uptime_ms=uptime,
            free_heap=heap,
            rssi=rssi,
            ip_address=ip,
        )
        device_name = device.name

    events = load_calendar_data(settings.calendar_data_path)
    messages, event_today = generate_messages(
        events,
        max_events=settings.max_events,
        max_future_days=settings.max_future_days,
    )

    return build_calendar_response(messages, event_today, device_name=device_name)
