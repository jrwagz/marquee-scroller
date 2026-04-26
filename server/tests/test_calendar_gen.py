"""Tests for calendar message generation."""

import datetime

from app.services.calendar_event import CalendarEvent
from app.services.calendar_gen import generate_messages, build_calendar_response

TODAY = datetime.date(2000, 1, 1)


def make_events(data_list):
    return [CalendarEvent(d, today=TODAY) for d in data_list]


def test_single_future_event():
    events = make_events([{"type": "birth", "date": "6-Jan-1999", "who": "Person1"}])
    msgs, event_today = generate_messages(events)
    assert not event_today
    assert msgs == ["5 days: Person1's 1st Birthday (6/Jan)"]


def test_birthday_today():
    events = make_events([{"type": "birth", "date": "1-Jan-1980", "who": "Person2"}])
    msgs, event_today = generate_messages(events)
    assert event_today
    assert msgs == ["Happy 20th Birthday Person2!!!"]


def test_two_events_today():
    events = make_events([
        {"type": "birth", "date": "1-Jan-1980", "who": "Person2"},
        {"type": "marriage", "date": "1-Jan-1990", "who": "Person3 & Person4"},
    ])
    msgs, event_today = generate_messages(events)
    assert event_today
    assert msgs == [
        "Happy 20th Birthday Person2!!!",
        "Happy 10th Anniversary Person3 & Person4!!!",
    ]


def test_today_plus_future_interleaved():
    events = make_events([
        {"type": "birth", "date": "1-Jan-1980", "who": "Person1"},
        {"type": "birth", "date": "2-Jan-1980", "who": "Person2"},
        {"type": "birth", "date": "3-Jan-1980", "who": "Person3"},
    ])
    msgs, event_today = generate_messages(events)
    assert event_today
    assert msgs == [
        "Happy 20th Birthday Person1!!!",
        "1 day: Person2's 20th Birthday (2/Jan)",
        "Happy 20th Birthday Person1!!!",
        "2 days: Person3's 20th Birthday (3/Jan)",
    ]


def test_no_events_in_range_shows_next():
    events = make_events([
        {"type": "birth", "date": "6-Jul-1999", "who": "Person1"},
        {"type": "birth", "date": "7-Jul-1999", "who": "Person2"},
    ])
    msgs, event_today = generate_messages(events)
    assert not event_today
    assert msgs == ["187 days: Person1's 1st Birthday (6/Jul)"]


def test_build_calendar_response_without_device_name():
    resp = build_calendar_response(["msg1", "msg2"], event_today=False)
    assert resp == [
        {"config": {"eventToday": 0}},
        {"message": "msg1"},
        {"message": "msg2"},
    ]


def test_build_calendar_response_with_device_name():
    resp = build_calendar_response(["msg1"], event_today=True, device_name="Kitchen Clock")
    assert resp == [
        {"config": {"eventToday": 1, "deviceName": "Kitchen Clock"}},
        {"message": "msg1"},
    ]
