"""Execute `.github/scripts/create_container_manifest.sh` and assert on the docker invocation it produces."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "create_container_manifest.sh"

IMAGE_NAME = "ghcr.io/cafecito-games/foundry"
AMD64_DIGEST = "sha256:" + "a" * 64
ARM64_DIGEST = "sha256:" + "b" * 64

FAKE_DOCKER = """#!/usr/bin/env python3
import json
import os
import sys

with open(os.environ["DOCKER_ARGV_JSON"], "w", encoding="utf-8") as handle:
    json.dump(sys.argv[1:], handle)
"""


class CreateContainerManifestTests(unittest.TestCase):
    def run_script(
        self,
        *,
        digests: dict[str, str],
        tags: str,
        annotations: str = "",
    ) -> tuple[subprocess.CompletedProcess[str], list[str]]:
        with tempfile.TemporaryDirectory() as workspace:
            digest_dir = Path(workspace) / "digests"
            digest_dir.mkdir()
            for name, digest in digests.items():
                (digest_dir / name).write_text(digest, encoding="utf-8")

            docker_bin = Path(workspace) / "fake-docker"
            docker_bin.write_text(FAKE_DOCKER, encoding="utf-8")
            docker_bin.chmod(0o755)
            argv_json = Path(workspace) / "docker-argv.json"

            environment = dict(os.environ)
            environment.update(
                {
                    "IMAGE_NAME": IMAGE_NAME,
                    "TAGS": tags,
                    "ANNOTATIONS": annotations,
                    "DOCKER_BIN": str(docker_bin),
                    "DOCKER_ARGV_JSON": str(argv_json),
                }
            )
            result = subprocess.run(
                [str(SCRIPT), str(digest_dir)],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            argv = json.loads(argv_json.read_text(encoding="utf-8")) if argv_json.exists() else []
            return result, argv

    def test_joins_every_digest_under_every_tag(self) -> None:
        result, argv = self.run_script(
            digests={"amd64": f"{AMD64_DIGEST}\n", "arm64": f"{ARM64_DIGEST}\n"},
            tags=f"{IMAGE_NAME}:v4.6.1\n{IMAGE_NAME}:latest",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(argv[:3], ["buildx", "imagetools", "create"])
        self.assertEqual(
            [argv[index + 1] for index, value in enumerate(argv) if value == "--tag"],
            [f"{IMAGE_NAME}:v4.6.1", f"{IMAGE_NAME}:latest"],
        )
        self.assertEqual(
            sorted(value for value in argv if value.startswith(f"{IMAGE_NAME}@")),
            sorted([f"{IMAGE_NAME}@{AMD64_DIGEST}", f"{IMAGE_NAME}@{ARM64_DIGEST}"]),
        )

    def test_attaches_index_annotations(self) -> None:
        result, argv = self.run_script(
            digests={"amd64": AMD64_DIGEST},
            tags=f"{IMAGE_NAME}:v4.6.1",
            annotations="index:org.opencontainers.image.version=4.6.1\nindex:org.opencontainers.image.revision=abc123",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            [argv[index + 1] for index, value in enumerate(argv) if value == "--annotation"],
            [
                "index:org.opencontainers.image.version=4.6.1",
                "index:org.opencontainers.image.revision=abc123",
            ],
        )

    def test_rejects_an_empty_digest_directory(self) -> None:
        result, argv = self.run_script(digests={}, tags=f"{IMAGE_NAME}:v4.6.1")

        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(argv, [])
        self.assertIn("No image digests found", result.stderr)

    def test_rejects_an_empty_digest_file(self) -> None:
        result, argv = self.run_script(
            digests={"amd64": AMD64_DIGEST, "arm64": "\n"},
            tags=f"{IMAGE_NAME}:v4.6.1",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(argv, [])
        self.assertIn("Empty image digest", result.stderr)

    def test_rejects_a_run_with_no_tags(self) -> None:
        result, argv = self.run_script(digests={"amd64": AMD64_DIGEST}, tags="")

        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(argv, [])
        self.assertIn("No image tags", result.stderr)


if __name__ == "__main__":
    unittest.main()
