"""Tests for `.github/scripts/resolve_container_alias.py`.

`should_publish_moving_alias` decides whether a release may move the `latest` or
`latest-<channel>` container tag onto itself. Getting it wrong republishes a
moving alias onto an older release, so the policy is exercised directly here
rather than inferred from the workflow that calls it.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import resolve_container_alias  # noqa: E402
from resolve_container_alias import release_key, should_publish_moving_alias  # noqa: E402

SCRIPT = SCRIPTS / "resolve_container_alias.py"


def release(
    tag_name: str,
    *,
    draft: bool = False,
    prerelease: bool | None = None,
    published_at: str | None = "2026-07-01T00:00:00Z",
) -> dict[str, Any]:
    if prerelease is None:
        prerelease = "-" in tag_name
    return {
        "tag_name": tag_name,
        "draft": draft,
        "prerelease": prerelease,
        "published_at": published_at,
    }


class ReleaseKeyTests(unittest.TestCase):
    def test_stable_tags_sort_above_every_prerelease_of_the_same_version(self) -> None:
        self.assertEqual((4, 6, 1, 0), release_key("v4.6.1", "stable"))
        self.assertEqual((4, 6, 1, 2), release_key("v4.6.1-beta.2", "beta"))
        newer = release_key("v4.6.1", "stable")
        older = release_key("v4.6.0", "stable")
        assert newer is not None and older is not None
        self.assertGreater(newer, older)

    def test_a_tag_from_another_channel_does_not_match(self) -> None:
        self.assertIsNone(release_key("v4.6.1-beta.2", "rc"))
        self.assertIsNone(release_key("v4.6.1", "beta"))
        self.assertIsNone(release_key("v4.6.1-beta.2", "stable"))

    def test_malformed_tags_do_not_match(self) -> None:
        for tag_name in ("4.6.1", "v4.6", "v4.6.1-beta", "v4.6.1-beta.0", "v4.6.1-delta.1", "v4.6.1-beta.01"):
            with self.subTest(tag_name=tag_name):
                self.assertIsNone(release_key(tag_name, "beta"))
                self.assertIsNone(release_key(tag_name, "stable"))


class ShouldPublishMovingAliasTests(unittest.TestCase):
    def test_the_newest_published_release_of_a_channel_moves_its_alias(self) -> None:
        pages = [[release("v4.6.1-beta.2"), release("v4.6.1-beta.1"), release("v4.6.0")]]
        self.assertTrue(should_publish_moving_alias(pages, "v4.6.1-beta.2", "beta"))

    def test_an_older_release_does_not_move_the_alias_backwards(self) -> None:
        pages = [[release("v4.6.1-beta.2"), release("v4.6.1-beta.1")]]
        self.assertFalse(should_publish_moving_alias(pages, "v4.6.1-beta.1", "beta"))

    def test_a_release_that_is_not_published_yet_does_not_move_the_alias(self) -> None:
        pages = [[release("v4.6.1-beta.2", published_at=None), release("v4.6.1-beta.1")]]
        self.assertFalse(should_publish_moving_alias(pages, "v4.6.1-beta.2", "beta"))

    def test_draft_releases_are_ignored_when_ranking_a_channel(self) -> None:
        pages = [[release("v4.6.1-beta.3", draft=True), release("v4.6.1-beta.2")]]
        self.assertTrue(should_publish_moving_alias(pages, "v4.6.1-beta.2", "beta"))
        self.assertFalse(should_publish_moving_alias(pages, "v4.6.1-beta.3", "beta"))

    def test_other_channels_do_not_rank_against_the_requested_channel(self) -> None:
        pages = [[release("v4.7.0-rc.1"), release("v4.6.1-beta.2"), release("v4.9.0")]]
        self.assertTrue(should_publish_moving_alias(pages, "v4.6.1-beta.2", "beta"))

    def test_every_page_of_the_paginated_listing_is_considered(self) -> None:
        pages = [[release("v4.6.1-beta.1")], [release("v4.6.1-beta.2")]]
        self.assertFalse(should_publish_moving_alias(pages, "v4.6.1-beta.1", "beta"))
        self.assertTrue(should_publish_moving_alias(pages, "v4.6.1-beta.2", "beta"))

    def test_an_unsupported_channel_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[]], "v4.6.1", "nightly")

    def test_a_tag_that_disagrees_with_its_channel_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[release("v4.6.1")]], "v4.6.1", "beta")

    def test_unpaginated_metadata_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([release("v4.6.1")], "v4.6.1", "stable")  # type: ignore[list-item]

    def test_a_non_object_release_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([["v4.6.1"]], "v4.6.1", "stable")  # type: ignore[list-item]

    def test_a_release_without_a_tag_name_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[{"draft": False, "prerelease": False}]], "v4.6.1", "stable")

    def test_missing_or_non_boolean_draft_and_prerelease_metadata_is_rejected(self) -> None:
        for field, value in (("draft", "false"), ("prerelease", 0), ("draft", None)):
            with self.subTest(field=field, value=value):
                candidate = release("v4.6.1", prerelease=False)
                candidate[field] = value
                with self.assertRaises(ValueError):
                    should_publish_moving_alias([[candidate]], "v4.6.1", "stable")

        for field in ("draft", "prerelease"):
            with self.subTest(missing=field):
                candidate = release("v4.6.1", prerelease=False)
                del candidate[field]
                with self.assertRaises(ValueError):
                    should_publish_moving_alias([[candidate]], "v4.6.1", "stable")

    def test_missing_or_blank_published_at_metadata_is_rejected(self) -> None:
        candidate = release("v4.6.1", prerelease=False)
        del candidate["published_at"]
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[candidate]], "v4.6.1", "stable")

        for blank in ("", "   ", 17):
            with self.subTest(published_at=blank):
                with self.assertRaises(ValueError):
                    should_publish_moving_alias(
                        [[release("v4.6.1", prerelease=False, published_at=blank)]],  # type: ignore[arg-type]
                        "v4.6.1",
                        "stable",
                    )

    def test_prerelease_metadata_must_agree_with_the_tag(self) -> None:
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[release("v4.6.1", prerelease=True)]], "v4.6.1", "stable")
        with self.assertRaises(ValueError):
            should_publish_moving_alias([[release("v4.6.1-beta.2", prerelease=False)]], "v4.6.1-beta.2", "beta")


class ResolveContainerAliasCommandTests(unittest.TestCase):
    def run_script(self, pages: object, release_tag: str, channel: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            releases_json = Path(directory) / "releases.json"
            releases_json.write_text(json.dumps(pages), encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--releases-json",
                    str(releases_json),
                    "--release-tag",
                    release_tag,
                    "--channel",
                    channel,
                ],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_the_command_prints_a_lowercase_boolean(self) -> None:
        completed = self.run_script([[release("v4.6.1-beta.2")]], "v4.6.1-beta.2", "beta")
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertEqual("true", completed.stdout.strip())

        completed = self.run_script(
            [[release("v4.6.1-beta.2"), release("v4.6.1-beta.1")]],
            "v4.6.1-beta.1",
            "beta",
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertEqual("false", completed.stdout.strip())

    def test_invalid_metadata_fails_the_command(self) -> None:
        completed = self.run_script([[release("v4.6.1", prerelease=True)]], "v4.6.1", "stable")
        self.assertEqual(1, completed.returncode)
        self.assertIn("Could not resolve moving container alias freshness", completed.stderr)

    def test_a_missing_metadata_file_fails_the_command(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--releases-json",
                str(Path(tempfile.gettempdir()) / "no-such-releases-file.json"),
                "--release-tag",
                "v4.6.1",
                "--channel",
                "stable",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(1, completed.returncode)
        self.assertIn("Could not resolve moving container alias freshness", completed.stderr)

    def test_the_supported_channels_are_the_documented_four(self) -> None:
        self.assertEqual({"alpha", "beta", "rc", "stable"}, resolve_container_alias.VALID_CHANNELS)


if __name__ == "__main__":
    unittest.main()
