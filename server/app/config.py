from pathlib import Path

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = "sqlite:///./data/wagfam.db"
    wagfam_api_key: str = ""
    calendar_data_path: Path = Path("data/wagfam_calendar_data.json")
    max_events: int = 10
    max_future_days: int = 14

    model_config = {"env_prefix": "WAGFAM_", "env_file": ".env"}


settings = Settings()
