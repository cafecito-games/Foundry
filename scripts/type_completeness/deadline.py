"""Two-business-day classification deadline at 17:00 America/New_York."""

from __future__ import annotations

from datetime import datetime, time, timedelta

from zoneinfo import ZoneInfo

NEW_YORK = ZoneInfo("America/New_York")
DEADLINE_TIME = time(17, 0)
BUSINESS_DAYS = 2


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
    return _require_aware(moment, "moment").isoformat()


def parse_timestamp(text: str) -> datetime:
    return _require_aware(datetime.fromisoformat(text), "timestamp")
