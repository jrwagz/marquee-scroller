"""Calendar event logic adapted from jrwagz/wagfam-clocks-data-source@ddd0f050.

The single behavioral change is the `today=` parameter on `CalendarEvent.__init__`
(replacing the source's module-level `todays_date()` indirection) so the class is
testable without monkey-patching. See `docs/CALENDAR_ADAPTATION.md` for the full
diff and rationale, and `docs/adaptations/calendar_event.diff` for the unified diff.
"""

import datetime
import functools
from enum import Enum, unique

MONTHS_IN_YEAR = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
]


@unique
class CalendarEventType(str, Enum):
    BIRTH = "birth"
    MARRIAGE = "marriage"


def parse_date(date_string: str) -> datetime.date:
    try:
        day_s, month_s, year_s = date_string.split("-")
        return datetime.date(int(year_s), MONTHS_IN_YEAR.index(month_s) + 1, int(day_s))
    except Exception as exc:
        raise ValueError(f"Invalid date string: {exc}")


def date_to_str(d: datetime.date) -> str:
    return f"{d.day}-{MONTHS_IN_YEAR[d.month - 1]}-{d.year}"


def next_occurrence_of_annual_event(
    event_date: datetime.date, today: datetime.date
) -> datetime.date:
    this_year = datetime.date(today.year, event_date.month, event_date.day)
    if this_year < today:
        return this_year.replace(year=today.year + 1)
    return this_year


def ordinal(n: int) -> str:
    if 11 <= (n % 100) <= 13:
        suffix = "th"
    else:
        suffix = ["th", "st", "nd", "rd", "th"][min(n % 10, 4)]
    return f"{n}{suffix}"


@functools.total_ordering
class CalendarEvent:
    def __init__(self, data_dict: dict, today: datetime.date | None = None):
        self.today = today or datetime.date.today()
        try:
            self.type = CalendarEventType(data_dict["type"])
            self.date = parse_date(data_dict["date"])
            self.next_occurrence = next_occurrence_of_annual_event(self.date, self.today)
            self.who = data_dict["who"]
        except KeyError as exc:
            raise ValueError(f"Invalid source dictionary! {exc}")

    @property
    def age_at_next_event(self) -> int:
        this_year_date = datetime.date(self.today.year, self.date.month, self.date.day)
        if this_year_date < self.today:
            years = self.today.year - self.date.year
        else:
            years = self.today.year - self.date.year - 1
        return years + 1

    @property
    def days_until_next_occurrence(self) -> int:
        return (self.next_occurrence - self.today).days

    @property
    def is_happening_today(self) -> bool:
        return self.today.month == self.date.month and self.today.day == self.date.day

    @property
    def next_occurrence_description(self) -> str:
        if self.type == CalendarEventType.BIRTH:
            event_name = "Birthday"
            if self.age_at_next_event == 0:
                event_name = "Due Date"
        elif self.type == CalendarEventType.MARRIAGE:
            event_name = "Anniversary"
            if self.age_at_next_event == 0:
                event_name = "Wedding"

        day_plurality = "s" if self.days_until_next_occurrence != 1 else ""
        number_display = f"{ordinal(self.age_at_next_event)} " if self.age_at_next_event != 0 else ""

        if self.is_happening_today:
            return f"Happy {number_display}{event_name} {self.who}!!!"
        return (
            f"{self.days_until_next_occurrence} day{day_plurality}: {self.who}'s "
            f"{number_display}{event_name} "
            f"({self.date.day}/{MONTHS_IN_YEAR[self.date.month - 1]})"
        )

    def __eq__(self, other):
        return self.next_occurrence == other.next_occurrence

    def __lt__(self, other):
        return self.next_occurrence < other.next_occurrence

    def __repr__(self):
        return (
            f"<CalendarEvent {self.type} {date_to_str(self.next_occurrence)} "
            f"{self.who} {self.age_at_next_event}>"
        )
