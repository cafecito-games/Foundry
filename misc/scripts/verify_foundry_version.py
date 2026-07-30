#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify release metadata emitted by a Foundry binary.")
    parser.add_argument("binary", help="Foundry executable to query")
    args = parser.parse_args()

    result = subprocess.run([args.binary, "--version", "--json"], check=False, capture_output=True, text=True)
    if result.returncode != 0:
        fail(f"{args.binary} --version --json exited with {result.returncode}: {result.stderr.strip()}")

    try:
        metadata = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        fail(f"{args.binary} emitted invalid version JSON: {exc}")

    expected = {
        "product": "Foundry",
        "version": os.environ.get("FOUNDRY_VERSION"),
        "release_tag": os.environ.get("FOUNDRY_RELEASE_TAG"),
        "channel": os.environ.get("FOUNDRY_CHANNEL"),
        "git_commit": os.environ.get("FOUNDRY_GIT_COMMIT"),
        "build_id": os.environ.get("FOUNDRY_BUILD_ID"),
    }
    for key, value in expected.items():
        if value is not None and metadata.get(key) != value:
            fail(f"{key}: expected {value!r}, got {metadata.get(key)!r}")

    if metadata.get("git_dirty") is not False:
        fail(f"git_dirty: expected false, got {metadata.get('git_dirty')!r}")
    if metadata.get("extension_api") != {"interface_format": 1, "abi_revision": 7}:
        fail(f"extension_api: unexpected value {metadata.get('extension_api')!r}")

    print(f"verified Foundry version metadata from {args.binary}")


if __name__ == "__main__":
    main()
