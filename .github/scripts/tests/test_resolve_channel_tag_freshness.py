"""Execute `.github/scripts/resolve_channel_tag_freshness.sh` and assert on the output it writes."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "resolve_channel_tag_freshness.sh"

RELEASE_TAG = "v4.6.1-beta.2"
RELEASE_CHANNEL = "beta"


def release(
    tag_name: str,
    *,
    draft: bool = False,
    published_at: str | None = "2026-07-25T12:00:00Z",
) -> dict[str, object]:
    return {
        "tag_name": tag_name,
        "draft": draft,
        "prerelease": True,
        "published_at": published_at,
    }


class ResolveChannelTagFreshnessTests(unittest.TestCase):
    def resolve(self, releases_body: str | None) -> tuple[subprocess.CompletedProcess[str], str]:
        """Run the script with the given releases-listing body, or no listing file at all."""
        with tempfile.TemporaryDirectory() as workspace:
            releases_json = Path(workspace) / "published-releases.json"
            if releases_body is not None:
                releases_json.write_text(releases_body, encoding="utf-8")
            github_output = Path(workspace) / "github-output"
            github_output.touch()

            environment = dict(os.environ)
            environment.update(
                {
                    "RELEASES_JSON": str(releases_json),
                    "RELEASE_TAG": RELEASE_TAG,
                    "RELEASE_CHANNEL": RELEASE_CHANNEL,
                    "GITHUB_OUTPUT": str(github_output),
                }
            )
            result = subprocess.run(
                [str(SCRIPT)],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            return result, github_output.read_text(encoding="utf-8")

    def assert_decision(self, releases_body: str | None, expected: str) -> subprocess.CompletedProcess[str]:
        result, output = self.resolve(releases_body)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, f"publish_channel_tag={expected}\n")
        return result

    def test_no_published_releases_publishes_the_channel_tag(self) -> None:
        self.assert_decision(json.dumps([[release(RELEASE_TAG)]]), "true")

    def test_newer_published_release_suppresses_the_channel_tag(self) -> None:
        pages = [[release(RELEASE_TAG), release("v4.6.1-beta.3")]]
        self.assert_decision(json.dumps(pages), "false")

    def test_older_published_release_publishes_the_channel_tag(self) -> None:
        pages = [[release("v4.6.1-beta.1")], [release(RELEASE_TAG)]]
        self.assert_decision(json.dumps(pages), "true")

    def test_draft_releases_are_ignored(self) -> None:
        pages = [[release(RELEASE_TAG), release("v4.6.1-beta.3", draft=True, published_at=None)]]
        self.assert_decision(json.dumps(pages), "true")

    def test_unlistable_releases_falls_back_to_exact_tag_only(self) -> None:
        result = self.assert_decision("{ this is not json", "false")
        self.assertIn("::warning::Could not evaluate moving container alias freshness", result.stdout)

    def test_absent_release_listing_falls_back_to_exact_tag_only(self) -> None:
        result = self.assert_decision(None, "false")
        self.assertIn("::warning::Could not list published GitHub releases", result.stdout)

    def test_unpublished_current_release_suppresses_the_channel_tag(self) -> None:
        pages = [[release(RELEASE_TAG, draft=True, published_at=None), release("v4.6.1-beta.1")]]
        self.assert_decision(json.dumps(pages), "false")


if __name__ == "__main__":
    unittest.main()
