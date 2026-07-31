#!/usr/bin/env python3
"""Select stale prerelease releases whose assets should be deleted.

Release assets never expire, so every prerelease permanently retains its full
set of build artifacts. Alpha builds are cut several times a day and each one
carries a ~1.1 GB export template bundle, so unbounded retention dominates the
repository's storage footprint within days.

This module only decides *which* releases are stale. The workflow performs the
deletion, so the selection rules stay directly testable.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

DEFAULT_MAX_AGE_HOURS = 24
DEFAULT_KEEP_NEWEST = 1


def parse_timestamp(value: str, tag_name: str, field: str) -> datetime:
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError(f"GitHub release {tag_name!r} has an unparsable {field!r} timestamp.") from exc
    if parsed.tzinfo is None:
        raise ValueError(f"GitHub release {tag_name!r} has a {field!r} timestamp without a timezone.")
    return parsed.astimezone(timezone.utc)


def published_prereleases(release_pages: list[list[dict[str, object]]]) -> list[tuple[datetime, str]]:
    """Return `(published_at, tag_name)` for every published, non-draft prerelease.

    Drafts and stable releases are never candidates, so they are dropped here
    rather than being filtered again by every caller.
    """

    candidates: list[tuple[datetime, str]] = []
    for page in release_pages:
        if not isinstance(page, list):
            raise ValueError("GitHub release metadata must contain paginated arrays.")
        for release in page:
            if not isinstance(release, dict):
                raise ValueError("GitHub release metadata contains a non-object release.")
            tag_name = release.get("tag_name")
            if not isinstance(tag_name, str) or not tag_name.strip():
                raise ValueError("GitHub release metadata contains a release without a tag name.")

            for boolean_field in ("draft", "prerelease"):
                if boolean_field not in release or type(release[boolean_field]) is not bool:
                    raise ValueError(f"GitHub release {tag_name!r} has invalid {boolean_field!r} metadata.")

            if release["draft"] or not release["prerelease"]:
                continue

            if "published_at" not in release:
                raise ValueError(f"GitHub release {tag_name!r} has no 'published_at' metadata.")
            published_at = release["published_at"]
            if published_at is None:
                continue
            if not isinstance(published_at, str) or not published_at.strip():
                raise ValueError(f"GitHub release {tag_name!r} has invalid 'published_at' metadata.")

            candidates.append((parse_timestamp(published_at, tag_name, "published_at"), tag_name))

    return candidates


def select_prereleases_to_prune(
    release_pages: list[list[dict[str, object]]],
    now: datetime,
    max_age_hours: int = DEFAULT_MAX_AGE_HOURS,
    keep_newest: int = DEFAULT_KEEP_NEWEST,
) -> list[str]:
    """Return the tags of prereleases that are both stale and not newest.

    The newest `keep_newest` prereleases are retained no matter how old they
    are: this repository's stable release is a draft, so GitHub reports no
    "latest" release at all, and an age-only rule would eventually delete every
    published download the project has.
    """

    if max_age_hours < 0:
        raise ValueError("max_age_hours must not be negative.")
    if keep_newest < 1:
        raise ValueError("keep_newest must retain at least one prerelease.")
    if now.tzinfo is None:
        raise ValueError("now must be timezone-aware.")

    candidates = published_prereleases(release_pages)
    # Sort newest first, breaking ties on the tag so equal timestamps are stable.
    candidates.sort(key=lambda candidate: (candidate[0], candidate[1]), reverse=True)

    cutoff = now.astimezone(timezone.utc) - timedelta(hours=max_age_hours)
    stale = [(published_at, tag) for published_at, tag in candidates[keep_newest:] if published_at < cutoff]
    stale.sort()
    return [tag for _, tag in stale]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="List prerelease tags whose assets are stale enough to delete.")
    parser.add_argument("--releases-json", type=Path, required=True)
    parser.add_argument("--max-age-hours", type=int, default=DEFAULT_MAX_AGE_HOURS)
    parser.add_argument("--keep-newest", type=int, default=DEFAULT_KEEP_NEWEST)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        with args.releases_json.open(encoding="utf-8") as releases_file:
            release_pages = json.load(releases_file)
        stale_tags = select_prereleases_to_prune(
            release_pages,
            datetime.now(timezone.utc),
            args.max_age_hours,
            args.keep_newest,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Could not select stale prereleases: {exc}", file=sys.stderr)
        return 1

    for tag in stale_tags:
        print(tag)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
