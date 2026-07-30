"""Execute `.github/scripts/verify_container_version_metadata.py` and assert on its exit code."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "verify_container_version_metadata.py"

ENGINE_VERSION = "4.6.1"
RELEASE_TAG = "v4.6.1-beta.2"
RELEASE_CHANNEL = "beta"
SOURCE_REVISION = "1234567890abcdef1234567890abcdef12345678"

CORRECT_METADATA: dict[str, object] = {
    "product": "Foundry",
    "version": ENGINE_VERSION,
    "release_tag": RELEASE_TAG,
    "channel": RELEASE_CHANNEL,
    "git_commit": SOURCE_REVISION,
}


def run_with(version_json: str) -> subprocess.CompletedProcess[str]:
    environment = {
        "PATH": "/usr/bin:/bin",
        "VERSION_JSON": version_json,
        "ENGINE_VERSION": ENGINE_VERSION,
        "RELEASE_TAG": RELEASE_TAG,
        "RELEASE_CHANNEL": RELEASE_CHANNEL,
        "SOURCE_REVISION": SOURCE_REVISION,
    }
    return subprocess.run(
        [sys.executable, str(SCRIPT)],
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )


def run_with_metadata(**overrides: object) -> subprocess.CompletedProcess[str]:
    metadata = dict(CORRECT_METADATA)
    for key, value in overrides.items():
        if value is None:
            metadata.pop(key, None)
        else:
            metadata[key] = value
    return run_with(json.dumps(metadata))


class VerifyContainerVersionMetadataTests(unittest.TestCase):
    def test_matching_metadata_exits_zero(self) -> None:
        result = run_with_metadata()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_wrong_product_is_reported_by_key(self) -> None:
        result = run_with_metadata(product="Godot")
        self.assertEqual(result.returncode, 1)
        self.assertIn("product:", result.stderr)
        self.assertIn("'Godot'", result.stderr)

    def test_wrong_version_is_reported_by_key(self) -> None:
        result = run_with_metadata(version="4.6.0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("version:", result.stderr)
        self.assertIn("'4.6.0'", result.stderr)

    def test_wrong_release_tag_is_reported_by_key(self) -> None:
        result = run_with_metadata(release_tag="v4.6.1-beta.1")
        self.assertEqual(result.returncode, 1)
        self.assertIn("release_tag:", result.stderr)
        self.assertIn("'v4.6.1-beta.1'", result.stderr)

    def test_wrong_channel_is_reported_by_key(self) -> None:
        result = run_with_metadata(channel="stable")
        self.assertEqual(result.returncode, 1)
        self.assertIn("channel:", result.stderr)
        self.assertIn("'stable'", result.stderr)

    def test_wrong_git_commit_is_reported_by_key(self) -> None:
        result = run_with_metadata(git_commit="0" * 40)
        self.assertEqual(result.returncode, 1)
        self.assertIn("git_commit:", result.stderr)
        self.assertIn("'" + "0" * 40 + "'", result.stderr)

    def test_missing_key_is_reported(self) -> None:
        result = run_with_metadata(release_tag=None)
        self.assertEqual(result.returncode, 1)
        self.assertIn("release_tag:", result.stderr)
        self.assertIn("got None", result.stderr)

    def test_every_mismatch_is_reported_not_just_the_first(self) -> None:
        result = run_with_metadata(product="Godot", channel="stable", git_commit="deadbeef")
        self.assertEqual(result.returncode, 1)
        for key in ("product:", "channel:", "git_commit:"):
            self.assertIn(key, result.stderr)
        self.assertNotIn("version:", result.stderr)

    def test_malformed_version_json_fails_with_a_clear_message(self) -> None:
        result = run_with("{not json")
        self.assertEqual(result.returncode, 1)
        self.assertIn("VERSION_JSON is not valid JSON", result.stderr)

    def test_non_object_version_json_fails_with_a_clear_message(self) -> None:
        result = run_with(json.dumps(["Foundry"]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("VERSION_JSON must be a JSON object", result.stderr)


if __name__ == "__main__":
    unittest.main()
