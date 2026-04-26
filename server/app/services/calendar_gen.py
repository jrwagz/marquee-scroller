"""Calendar message generation adapted from jrwagz/wagfam-clocks-data-source."""

import datetime
import json
from pathlib import Path

from app.services.calendar_event import CalendarEvent


def load_calendar_data(path: Path, today: datetime.date | None = None) -> list[CalendarEvent]:
    data = json.loads(path.read_text())
    return [CalendarEvent(item, today=today) for item in data]


def generate_messages(
    events: list[CalendarEvent],
    max_events: int = 10,
    max_future_days: int = 14,
) -> tuple[list[str], bool]:
    """Generate calendar messages from a list of events.

    Returns (messages, event_today) where messages is a list of strings
    and event_today indicates if any event is happening today.
    """
    sorted_events = sorted(events)
    event_today = False

    events_today_msgs: list[str] = []
    events_future_msgs: list[str] = []

    for ev in sorted_events:
        if ev.is_happening_today:
            events_today_msgs.append(ev.next_occurrence_description)
            event_today = True
        else:
            break

    for ev in sorted_events:
        if not ev.is_happening_today and ev.days_until_next_occurrence <= max_future_days:
            events_future_msgs.append(ev.next_occurrence_description)

    messages: list[str] = []

    if events_today_msgs and not events_future_msgs:
        messages.extend(events_today_msgs)
    elif events_today_msgs and events_future_msgs:
        total = 0
        for future_msg in events_future_msgs:
            if total >= max_events:
                break
            messages.extend(events_today_msgs)
            messages.append(future_msg)
            total += len(events_today_msgs) + 1
    elif events_future_msgs:
        messages.extend(events_future_msgs[:max_events])
    else:
        for ev in sorted_events:
            if not ev.is_happening_today and ev.days_until_next_occurrence >= max_future_days:
                messages.append(ev.next_occurrence_description)
                break

    return messages, event_today


def build_calendar_response(
    messages: list[str],
    event_today: bool,
    device_name: str | None = None,
) -> list[dict]:
    """Build the JSON response in the format the clocks expect."""
    config: dict = {"eventToday": int(event_today)}
    if device_name:
        config["deviceName"] = device_name

    result: list[dict] = [{"config": config}]
    for msg in messages:
        result.append({"message": msg})
    return result
