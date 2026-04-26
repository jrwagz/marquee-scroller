import datetime
import json
from pathlib import Path

import pytest
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker
from sqlalchemy.pool import StaticPool
from starlette.testclient import TestClient

from app.database import Base, get_db
from app.config import settings
from app.main import app


@pytest.fixture()
def today():
    """Fixed date for deterministic tests."""
    return datetime.date(2000, 1, 1)


@pytest.fixture()
def db_session():
    """In-memory SQLite session shared across threads via StaticPool."""
    engine = create_engine(
        "sqlite://",
        connect_args={"check_same_thread": False},
        poolclass=StaticPool,
    )
    Base.metadata.create_all(bind=engine)
    Session = sessionmaker(bind=engine)
    session = Session()
    yield session
    session.close()


@pytest.fixture()
def client(db_session, tmp_path):
    """FastAPI test client with overridden DB and calendar data."""
    cal_path = tmp_path / "cal.json"
    cal_path.write_text(json.dumps([
        {"type": "birth", "date": "6-Jan-1999", "who": "TestPerson"},
    ]))
    settings.calendar_data_path = cal_path
    settings.wagfam_api_key = "test-key"

    def override_db():
        yield db_session

    app.dependency_overrides[get_db] = override_db

    with TestClient(app) as c:
        yield c

    app.dependency_overrides.clear()
