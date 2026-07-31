"""Tests for `.github/scripts/prune_prereleases.py`.

`select_prereleases_to_prune` drives an irreversible deletion, so every
retention rule that stands between the workflow and a published download is
exercised directly here rather than inferred from the workflow that calls it.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

from prune_prereleases import published_prereleases, select_prereleases_to_prune  # noqa: E402

SCRIPT = SCRIPTS / "prune_prereleases.py"

NOW = datetime(2026, 7, 31, 12, 0, 0, tzinfo=timezone.utc)

# `published_at=None` means "unpublished", so it cannot double as "unset".
DERIVE_FROM_HOURS_AGO = object()


def stamp(hours_ago: float) -> str:
    return (NOW - timedelta(hours=hours_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


def release(
    tag_name: str,
    *,
    hours_ago: float = 100,
    draft: bool = False,
    prerelease: bool = True,
    published_at: Any = DERIVE_FROM_HOURS_AGO,
) -> dict[str, Any]:
    if published_at is DERIVE_FROM_HOURS_AGO:
        published_at = stamp(hours_ago)
    return {
        "tag_name": tag_name,
        "draft": draft,
        "prerelease": prerelease,
        "published_at": published_at,
    }


class SelectPrereleasesToPruneTests(unittest.TestCase):
    def test_prunes_prereleases_older_than_the_age_limit(self):
        pages = [[release("v1.0.0-alpha.3", hours_ago=1), release("v1.0.0-alpha.2", hours_ago=48)]]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), ["v1.0.0-alpha.2"])

    def test_retains_the_newest_prerelease_however_old_it_is(self):
        pages = [[release("v1.0.0-alpha.1", hours_ago=5000)]]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), [])

    def test_retains_the_newest_prerelease_even_when_every_release_is_stale(self):
        pages = [
            [
                release("v1.0.0-alpha.2", hours_ago=30),
                release("v1.0.0-alpha.1", hours_ago=90),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), ["v1.0.0-alpha.1"])

    def test_keep_newest_can_retain_more_than_one_prerelease(self):
        pages = [
            [
                release("v1.0.0-alpha.3", hours_ago=30),
                release("v1.0.0-alpha.2", hours_ago=60),
                release("v1.0.0-alpha.1", hours_ago=90),
            ]
        ]

        self.assertEqual(
            select_prereleases_to_prune(pages, NOW, keep_newest=2),
            ["v1.0.0-alpha.1"],
        )

    def test_newest_is_decided_by_timestamp_not_page_order(self):
        pages = [
            [release("v1.0.0-alpha.1", hours_ago=90)],
            [release("v1.0.0-alpha.2", hours_ago=2)],
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), ["v1.0.0-alpha.1"])

    def test_never_prunes_a_stable_release(self):
        pages = [
            [
                release("v1.0.0", hours_ago=5000, prerelease=False),
                release("v1.0.0-alpha.1", hours_ago=1),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), [])

    def test_never_prunes_a_draft(self):
        pages = [
            [
                release("v1.0.0-alpha.9", hours_ago=1),
                release("v1.0.0-alpha.8", hours_ago=5000, draft=True),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), [])

    def test_a_draft_does_not_consume_the_retention_slot(self):
        pages = [
            [
                release("v1.0.0-alpha.3", hours_ago=1, draft=True),
                release("v1.0.0-alpha.2", hours_ago=48),
                release("v1.0.0-alpha.1", hours_ago=96),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), ["v1.0.0-alpha.1"])

    def test_unpublished_prereleases_are_skipped(self):
        pages = [[release("v1.0.0-alpha.2", hours_ago=1), release("v1.0.0-alpha.1", published_at=None)]]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), [])

    def test_a_release_exactly_at_the_age_limit_is_retained(self):
        pages = [
            [
                release("v1.0.0-alpha.3", hours_ago=0),
                release("v1.0.0-alpha.2", hours_ago=24),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW), [])

    def test_results_are_ordered_oldest_first(self):
        pages = [
            [
                release("v1.0.0-alpha.4", hours_ago=1),
                release("v1.0.0-alpha.2", hours_ago=48),
                release("v1.0.0-alpha.1", hours_ago=96),
                release("v1.0.0-alpha.3", hours_ago=25),
            ]
        ]

        self.assertEqual(
            select_prereleases_to_prune(pages, NOW),
            ["v1.0.0-alpha.1", "v1.0.0-alpha.2", "v1.0.0-alpha.3"],
        )

    def test_offset_timestamps_are_normalised_to_utc(self):
        pages = [
            [
                release("v1.0.0-alpha.2", published_at="2026-07-31T11:00:00Z"),
                # 08:00-05:00 is 13:00Z, so this is the newest release despite
                # its wall-clock time reading earlier than the one above.
                release("v1.0.0-alpha.1", published_at="2026-07-31T08:00:00-05:00"),
            ]
        ]

        self.assertEqual(select_prereleases_to_prune(pages, NOW, max_age_hours=0), ["v1.0.0-alpha.2"])

    def test_empty_release_list_prunes_nothing(self):
        self.assertEqual(select_prereleases_to_prune([[]], NOW), [])

    def test_keep_newest_below_one_is_rejected(self):
        with self.assertRaises(ValueError):
            select_prereleases_to_prune([[release("v1.0.0-alpha.1")]], NOW, keep_newest=0)

    def test_negative_age_limit_is_rejected(self):
        with self.assertRaises(ValueError):
            select_prereleases_to_prune([[release("v1.0.0-alpha.1")]], NOW, max_age_hours=-1)

    def test_naive_now_is_rejected(self):
        with self.assertRaises(ValueError):
            select_prereleases_to_prune([[release("v1.0.0-alpha.1")]], datetime(2026, 7, 31, 12, 0, 0))


class MalformedMetadataTests(unittest.TestCase):
    def test_non_paginated_metadata_is_rejected(self):
        with self.assertRaises(ValueError):
            published_prereleases([release("v1.0.0-alpha.1")])

    def test_missing_tag_name_is_rejected(self):
        payload = release("v1.0.0-alpha.1")
        del payload["tag_name"]

        with self.assertRaises(ValueError):
            published_prereleases([[payload]])

    def test_missing_prerelease_flag_is_rejected(self):
        payload = release("v1.0.0-alpha.1")
        del payload["prerelease"]

        with self.assertRaises(ValueError):
            published_prereleases([[payload]])

    def test_missing_published_at_is_rejected(self):
        payload = release("v1.0.0-alpha.1")
        del payload["published_at"]

        with self.assertRaises(ValueError):
            published_prereleases([[payload]])

    def test_unparseable_published_at_is_rejected(self):
        with self.assertRaises(ValueError):
            published_prereleases([[release("v1.0.0-alpha.1", published_at="last tuesday")]])


class CommandLineTests(unittest.TestCase):
    def run_script(self, release_pages: Any, *args: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            releases_json = Path(directory) / "releases.json"
            releases_json.write_text(json.dumps(release_pages), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(SCRIPT), "--releases-json", str(releases_json), *args],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_stale_tags_are_printed_one_per_line(self):
        # Far enough in the past that these stay stale regardless of run date.
        pages = [
            [
                {
                    "tag_name": "v1.0.0-alpha.2",
                    "draft": False,
                    "prerelease": True,
                    "published_at": "2020-01-02T00:00:00Z",
                },
                {
                    "tag_name": "v1.0.0-alpha.1",
                    "draft": False,
                    "prerelease": True,
                    "published_at": "2020-01-01T00:00:00Z",
                },
            ]
        ]

        result = self.run_script(pages)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.split(), ["v1.0.0-alpha.1"])

    def test_malformed_metadata_fails_without_printing_tags(self):
        result = self.run_script({"not": "pages"})

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout.strip(), "")
        self.assertIn("Could not select stale prereleases", result.stderr)


if __name__ == "__main__":
    unittest.main()
