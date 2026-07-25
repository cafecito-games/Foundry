#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

VALID_CHANNELS = {"alpha", "beta", "rc", "stable"}
STABLE_TAG_RE = re.compile(r"^v(?P<major>[0-9]+)\.(?P<minor>[0-9]+)\.(?P<patch>[0-9]+)$")
PRERELEASE_TAG_RE = re.compile(
    r"^v(?P<major>[0-9]+)\.(?P<minor>[0-9]+)\.(?P<patch>[0-9]+)"
    r"-(?P<channel>alpha|beta|rc)\.(?P<number>[1-9][0-9]*)$"
)


def release_key(tag_name: str, expected_channel: str) -> tuple[int, int, int, int] | None:
    stable_match = STABLE_TAG_RE.fullmatch(tag_name)
    prerelease_match = PRERELEASE_TAG_RE.fullmatch(tag_name)
    if stable_match is not None:
        channel = "stable"
        number = 0
        match = stable_match
    elif prerelease_match is not None:
        channel = prerelease_match.group("channel")
        number = int(prerelease_match.group("number"))
        match = prerelease_match
    else:
        return None

    if channel != expected_channel:
        return None
    return (
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch")),
        number,
    )


def should_publish_moving_alias(
    release_pages: list[list[dict[str, object]]],
    release_tag: str,
    channel: str,
) -> bool:
    if channel not in VALID_CHANNELS:
        raise ValueError(f"Unsupported release channel {channel!r}.")

    current_key = release_key(release_tag, channel)
    if current_key is None:
        raise ValueError(f"Release tag {release_tag!r} does not match channel {channel!r}.")

    published_keys: list[tuple[int, int, int, int]] = []
    current_is_published = False
    for page in release_pages:
        if not isinstance(page, list):
            raise ValueError("GitHub release metadata must contain paginated arrays.")
        for release in page:
            if not isinstance(release, dict):
                raise ValueError("GitHub release metadata contains a non-object release.")
            tag_name = release.get("tag_name")
            if not isinstance(tag_name, str):
                raise ValueError("GitHub release metadata contains a release without a tag name.")

            key = release_key(tag_name, channel)
            if key is None:
                continue

            for boolean_field in ("draft", "prerelease"):
                if boolean_field not in release or type(release[boolean_field]) is not bool:
                    raise ValueError(f"GitHub release {tag_name!r} has invalid {boolean_field!r} metadata.")
            if "published_at" not in release:
                raise ValueError(f"GitHub release {tag_name!r} has no 'published_at' metadata.")
            published_at = release["published_at"]
            if published_at is not None and (not isinstance(published_at, str) or not published_at.strip()):
                raise ValueError(f"GitHub release {tag_name!r} has invalid 'published_at' metadata.")

            if release["draft"] or published_at is None:
                continue
            if release["prerelease"] is not (channel != "stable"):
                continue

            published_keys.append(key)
            if tag_name == release_tag:
                current_is_published = True

    return current_is_published and current_key == max(published_keys)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Decide whether a release may update its moving container alias.")
    parser.add_argument("--releases-json", type=Path, required=True)
    parser.add_argument("--release-tag", required=True)
    parser.add_argument("--channel", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        with args.releases_json.open(encoding="utf-8") as releases_file:
            release_pages = json.load(releases_file)
        publish_alias = should_publish_moving_alias(release_pages, args.release_tag, args.channel)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Could not resolve moving container alias freshness: {exc}", file=sys.stderr)
        return 1

    print(str(publish_alias).lower())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
