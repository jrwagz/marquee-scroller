"""Tests for CalendarEvent, adapted from jrwagz/wagfam-clocks-data-source.

All tests use today = 1-Jan-2000 for determinism.
"""

import datetime

import pytest

from app.services.calendar_event import CalendarEvent, CalendarEventType

TODAY = datetime.date(2000, 1, 1)


def make_event(data):
    return CalendarEvent(data, today=TODAY)


def test_simple_case():
    event = make_event({"type": "birth", "date": "6-Jan-1999", "who": "Person1"})
    assert event.age_at_next_event == 1
    assert event.next_occurrence == datetime.date(2000, 1, 6)
    assert event.next_occurrence_description == "5 days: Person1's 1st Birthday (6/Jan)"
    assert event.type == CalendarEventType.BIRTH
    assert not event.is_happening_today
    assert event.days_until_next_occurrence == 5


def test_is_happening_today():
    event = make_event({"type": "birth", "date": "1-Jan-1980", "who": "Person2"})
    assert event.age_at_next_event == 20
    assert event.next_occurrence_description == "Happy 20th Birthday Person2!!!"
    assert event.is_happening_today
    assert event.days_until_next_occurrence == 0


def test_one_day_till_event():
    event = make_event({"type": "birth", "date": "2-Jan-1999", "who": "Person1"})
    assert event.age_at_next_event == 1
    assert event.next_occurrence_description == "1 day: Person1's 1st Birthday (2/Jan)"
    assert event.days_until_next_occurrence == 1


def test_due_date():
    event = make_event({"type": "birth", "date": "14-Jan-2000", "who": "Sky"})
    assert event.age_at_next_event == 0
    assert event.next_occurrence_description == "13 days: Sky's Due Date (14/Jan)"
    assert event.days_until_next_occurrence == 13


def test_wedding():
    event = make_event({"type": "marriage", "date": "4-Jan-2000", "who": "Aubrielle & David"})
    assert event.age_at_next_event == 0
    assert event.next_occurrence_description == "3 days: Aubrielle & David's Wedding (4/Jan)"
    assert event.type == CalendarEventType.MARRIAGE
    assert event.days_until_next_occurrence == 3


def test_invalid_data_raises():
    with pytest.raises(ValueError):
        make_event({"type": "birth", "date": "6-Jan-1999"})  # missing 'who'


def test_sorting():
    e1 = make_event({"type": "birth", "date": "10-Jan-1999", "who": "A"})
    e2 = make_event({"type": "birth", "date": "5-Jan-1999", "who": "B"})
    assert sorted([e1, e2]) == [e2, e1]
