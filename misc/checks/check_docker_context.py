#!/usr/bin/env python3
"""Check that every file the headless Dockerfile copies survives its `.dockerignore`.

`docker/.dockerignore` denies everything and re-includes the staged editor binaries by
name, so renaming or adding one without updating it produces a build that fails only
once a release is already underway, with `"/<name>": not found` from BuildKit.

`find_context_violations` is pure over the two file bodies and is driven by
`misc/checks/tests/test_check_docker_context.py`.
"""

from __future__ import annotations

import re
import sys
from fnmatch import fnmatch
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DOCKERFILE = REPO_ROOT / "docker/foundry-headless.Dockerfile"
DOCKERIGNORE = REPO_ROOT / "docker/.dockerignore"

# The architectures the release workflow builds. `TARGETARCH` takes one of these in
# every real build, so a COPY that interpolates it has to resolve under each.
TARGET_ARCHITECTURES = ("amd64", "arm64")

COPY_RE = re.compile(r"^\s*COPY\s+(?P<arguments>.*)$", re.IGNORECASE)
ARG_INTERPOLATION_RE = re.compile(r"\$\{?(?P<name>[A-Za-z_][A-Za-z0-9_]*)\}?")


def copy_sources(dockerfile: str) -> list[str]:
    """Return every local path a Dockerfile's COPY instructions read from the context."""

    sources: list[str] = []
    for line in dockerfile.splitlines():
        match = COPY_RE.match(line)
        if match is None:
            continue

        arguments = [argument for argument in match.group("arguments").split() if not argument.startswith("--")]
        if any(flag.startswith("--from=") for flag in match.group("arguments").split()):
            # Copied from another build stage, not from the context.
            continue
        # The final argument is the destination inside the image.
        sources.extend(arguments[:-1])
    return sources


def expand_target_architectures(source: str) -> list[str]:
    """Expand a COPY source over every architecture `TARGETARCH` can hold."""

    if ARG_INTERPOLATION_RE.search(source) is None:
        return [source]
    return [ARG_INTERPOLATION_RE.sub(architecture, source) for architecture in TARGET_ARCHITECTURES]


def is_included(path: str, dockerignore: str) -> bool:
    """Resolve a path against `.dockerignore` rules, where the last match wins."""

    included = True
    for raw_line in dockerignore.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        negated = line.startswith("!")
        pattern = line[1:] if negated else line
        if fnmatch(path, pattern):
            included = negated
    return included


def find_context_violations(dockerfile: str, dockerignore: str) -> list[str]:
    """Return every COPY source that `.dockerignore` keeps out of the build context."""

    violations: list[str] = []
    for source in copy_sources(dockerfile):
        for expanded in expand_target_architectures(source):
            if not is_included(expanded, dockerignore):
                violations.append(
                    f"docker/foundry-headless.Dockerfile copies {expanded!r}, "
                    f"but docker/.dockerignore excludes it from the build context"
                )
    return violations


def main() -> int:
    violations = find_context_violations(
        DOCKERFILE.read_text(encoding="utf-8"),
        DOCKERIGNORE.read_text(encoding="utf-8"),
    )
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print("docker context check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
