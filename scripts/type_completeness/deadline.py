"""Two-business-day classification deadline at 17:00 America/New_York.

The repository targets Python 3.8, which has no ``zoneinfo``, so America/New_York is modeled directly with the
United States daylight-saving rule in force since 2007: UTC-5, switching to UTC-4 from the second Sunday of March
at 02:00 local until the first Sunday of November at 02:00 local.
"""

from __future__ import annotations

import re
from datetime import date, datetime, time, timedelta, timezone, tzinfo
from typing import Optional

STANDARD_OFFSET = timedelta(hours=-5)
DAYLIGHT_DELTA = timedelta(hours=1)
DEADLINE_TIME = time(17, 0)
BUSINESS_DAYS = 2


def _nth_sunday(year: int, month: int, ordinal: int) -> date:
    first = date(year, month, 1)
    offset = (6 - first.weekday()) % 7
    return first + timedelta(days=offset + 7 * (ordinal - 1))


class NewYork(tzinfo):
    """America/New_York with the post-2007 United States daylight-saving rule."""

    def _in_daylight_time(self, moment: datetime) -> bool:
        # Wall-clock bounds: DST starts at 02:00 standard time and ends at 02:00 daylight time.
        start = datetime.combine(_nth_sunday(moment.year, 3, 2), time(2, 0))
        end = datetime.combine(_nth_sunday(moment.year, 11, 1), time(2, 0))
        naive = moment.replace(tzinfo=None, fold=0)
        if moment.fold and end - timedelta(hours=1) <= naive < end:
            # The 01:00 hour repeats when daylight time ends; fold=1 is its second, standard-time occurrence.
            return False
        return start <= naive < end

    def utcoffset(self, moment: Optional[datetime]) -> timedelta:
        if moment is None:
            return STANDARD_OFFSET
        return STANDARD_OFFSET + self.dst(moment)

    def dst(self, moment: Optional[datetime]) -> timedelta:
        if moment is None:
            return timedelta(0)
        return DAYLIGHT_DELTA if self._in_daylight_time(moment) else timedelta(0)

    def tzname(self, moment: Optional[datetime]) -> str:
        return "EDT" if moment is not None and self._in_daylight_time(moment) else "EST"

    def fromutc(self, moment: datetime) -> datetime:
        standard = moment + STANDARD_OFFSET
        # A UTC instant is in daylight time when its standard-time wall clock is past the 02:00 start and before
        # the 01:00 standard-time equivalent of the 02:00 daylight-time end.
        start = datetime.combine(_nth_sunday(moment.year, 3, 2), time(2, 0))
        end = datetime.combine(_nth_sunday(moment.year, 11, 1), time(1, 0))
        naive_standard = standard.replace(tzinfo=None)
        if start <= naive_standard < end:
            return standard + DAYLIGHT_DELTA
        if end <= naive_standard < end + DAYLIGHT_DELTA:
            return standard.replace(fold=1)
        return standard

    def __repr__(self) -> str:
        return "America/New_York"


NEW_YORK = NewYork()


def _require_aware(moment: datetime, name: str) -> datetime:
    if moment.tzinfo is None or moment.utcoffset() is None:
        raise ValueError(f"{name} must be timezone-aware")
    return moment.astimezone(NEW_YORK)


def classification_deadline(detected_at: datetime) -> datetime:
    """17:00 New York on the second weekday strictly after the detection date; weekends are skipped."""
    local = _require_aware(detected_at, "detected_at")
    day = local.date()
    remaining = BUSINESS_DAYS
    while remaining > 0:
        day += timedelta(days=1)
        if day.weekday() < 5:
            remaining -= 1
    return datetime.combine(day, DEADLINE_TIME, tzinfo=NEW_YORK)


def is_overdue(due_at: datetime, now: datetime) -> bool:
    return _require_aware(now, "now") > _require_aware(due_at, "due_at")


def format_timestamp(moment: datetime) -> str:
    """Render in America/New_York, the zone the classification deadline is defined in."""
    return _require_aware(moment, "moment").isoformat()


def format_utc_timestamp(moment: datetime) -> str:
    """Render as UTC with the ``Z`` designator; ``parse_timestamp`` reads it back unchanged."""
    utc = _require_aware(moment, "moment").astimezone(timezone.utc)
    return utc.strftime("%Y-%m-%dT%H:%M:%S") + (f".{utc.microsecond:06d}" if utc.microsecond else "") + "Z"


# YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|+HHMM). Python 3.8's datetime.fromisoformat rejects ``Z``, offsets
# without a colon, and fractions that are not 3 or 6 digits, so the format is parsed explicitly instead.
_TIMESTAMP = re.compile(
    r"^(?P<year>\d{4})-(?P<month>\d{2})-(?P<day>\d{2})T(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})"
    r"(?:\.(?P<fraction>\d{1,9}))?(?P<offset>Z|[+-]\d{2}:?\d{2})$"
)


def parse_timestamp(text: str) -> datetime:
    """Parse an ISO-8601 timestamp with an explicit offset and return it normalized to UTC."""
    match = _TIMESTAMP.match(text)
    if match is None:
        raise ValueError(
            f"{text!r} is not an ISO-8601 timestamp with an explicit offset "
            "(expected YYYY-MM-DDTHH:MM:SS[.ffffff](Z|+HH:MM|+HHMM))"
        )
    fraction = match.group("fraction") or ""
    microsecond = int((fraction + "000000")[:6]) if fraction else 0
    offset_text = match.group("offset")
    if offset_text == "Z":
        offset = timedelta(0)
    else:
        sign = 1 if offset_text[0] == "+" else -1
        digits = offset_text[1:].replace(":", "")
        offset_hours, offset_minutes = int(digits[:2]), int(digits[2:])
        if offset_hours > 23 or offset_minutes > 59:
            raise ValueError(f"{text!r} is not an ISO-8601 timestamp: offset {offset_text!r} is out of range")
        offset = sign * timedelta(hours=offset_hours, minutes=offset_minutes)
    try:
        local = datetime(
            int(match.group("year")),
            int(match.group("month")),
            int(match.group("day")),
            int(match.group("hour")),
            int(match.group("minute")),
            int(match.group("second")),
            microsecond,
            tzinfo=timezone(offset),
        )
    except ValueError as error:
        raise ValueError(f"{text!r} is not a valid ISO-8601 timestamp: {error}") from error
    return local.astimezone(timezone.utc)
