# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_docker_context.py.

These drive the check's pure Dockerfile/`.dockerignore` comparison with synthetic
text so each violation class is proven to fire without running a container build.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_docker_context  # noqa: E402

DOCKERFILE = """\
FROM ubuntu:24.04
ARG TARGETARCH
RUN apt-get update
COPY --chown=10001:10001 --chmod=0755 foundry-${TARGETARCH} /usr/local/bin/foundry
"""

DOCKERIGNORE = """\
*
!foundry-amd64
!foundry-arm64
"""


class DockerContextTests(unittest.TestCase):
    def test_the_real_dockerfile_and_dockerignore_agree(self) -> None:
        violations = check_docker_context.find_context_violations(
            (REPO_ROOT / "docker/foundry-headless.Dockerfile").read_text(encoding="utf-8"),
            (REPO_ROOT / "docker/.dockerignore").read_text(encoding="utf-8"),
        )

        self.assertEqual(violations, [])

    def test_a_copy_source_excluded_by_a_deny_all_is_reported(self) -> None:
        violations = check_docker_context.find_context_violations(DOCKERFILE, "*\n")

        self.assertEqual(len(violations), 2)
        self.assertIn("foundry-amd64", violations[0])
        self.assertIn("foundry-arm64", violations[1])

    def test_every_architecture_is_reported_not_just_the_first(self) -> None:
        # Exactly the regression that broke the release: one architecture re-included,
        # the other silently left out of the context.
        violations = check_docker_context.find_context_violations(DOCKERFILE, "*\n!foundry-amd64\n")

        self.assertEqual(len(violations), 1)
        self.assertIn("foundry-arm64", violations[0])

    def test_a_stale_re_include_name_is_reported(self) -> None:
        violations = check_docker_context.find_context_violations(DOCKERFILE, "*\n!foundry.linuxbsd.editor.x86_64\n")

        self.assertEqual(len(violations), 2)

    def test_an_uninterpolated_source_is_checked_verbatim(self) -> None:
        dockerfile = "FROM ubuntu:24.04\nCOPY entrypoint.sh /entrypoint.sh\n"

        self.assertEqual(check_docker_context.find_context_violations(dockerfile, "*\n!entrypoint.sh\n"), [])
        self.assertEqual(len(check_docker_context.find_context_violations(dockerfile, "*\n")), 1)

    def test_a_stage_to_stage_copy_is_not_a_context_read(self) -> None:
        dockerfile = (
            "FROM ubuntu:24.04 AS build\nFROM ubuntu:24.04\nCOPY --from=build /out/foundry /usr/local/bin/foundry\n"
        )

        self.assertEqual(check_docker_context.find_context_violations(dockerfile, "*\n"), [])

    def test_no_dockerignore_rules_leave_every_source_included(self) -> None:
        self.assertEqual(check_docker_context.find_context_violations(DOCKERFILE, "# nothing denied\n"), [])


if __name__ == "__main__":
    unittest.main()
