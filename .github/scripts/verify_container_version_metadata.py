#!/usr/bin/env python3
"""Verify the `--version --json` metadata reported by the headless container image.

Reads the captured JSON document from ``VERSION_JSON`` and the expected values from
``ENGINE_VERSION``, ``RELEASE_TAG``, ``RELEASE_CHANNEL``, and ``SOURCE_REVISION``.
Every mismatching key is reported, not just the first one, so a single container
verification run explains all of the drift it found.
"""

from __future__ import annotations

import json
import os
import sys

EXPECTED_PRODUCT = "Foundry"


def expected_metadata(environment: dict[str, str]) -> dict[str, str]:
    return {
        "product": EXPECTED_PRODUCT,
        "version": environment["ENGINE_VERSION"],
        "release_tag": environment["RELEASE_TAG"],
        "channel": environment["RELEASE_CHANNEL"],
        "git_commit": environment["SOURCE_REVISION"],
    }


def collect_mismatches(metadata: object, expected: dict[str, str]) -> list[str]:
    if not isinstance(metadata, dict):
        return [f"VERSION_JSON must be a JSON object, got {type(metadata).__name__}"]
    return [
        f"{key}: expected {value!r}, got {metadata.get(key)!r}"
        for key, value in expected.items()
        if metadata.get(key) != value
    ]


def main() -> int:
    environment = os.environ
    try:
        metadata = json.loads(environment["VERSION_JSON"])
    except json.JSONDecodeError as exc:
        print(f"VERSION_JSON is not valid JSON: {exc}", file=sys.stderr)
        return 1

    mismatches = collect_mismatches(metadata, expected_metadata(dict(environment)))
    if mismatches:
        for mismatch in mismatches:
            print(mismatch, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
