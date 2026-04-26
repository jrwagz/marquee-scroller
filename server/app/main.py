from fastapi import FastAPI

from app.database import Base, engine
from app.routers import calendar, devices

app = FastAPI(title="WagFam CalClock Server", version="1.0.0")

Base.metadata.create_all(bind=engine)

app.include_router(calendar.router)
app.include_router(devices.router)


@app.get("/health")
def health():
    return {"status": "ok", "version": "1.0.0"}
