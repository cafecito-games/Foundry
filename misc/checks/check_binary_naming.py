#!/usr/bin/env python3
"""Scan a built Foundry binary for leaked upstream identifiers.

Needs a compiled artifact, so it is invoked from build/release workflows rather
than from `pre-commit`. The token matching itself is pure and lives in
`find_forbidden_tokens`, which `misc/checks/tests/test_check_binary_naming.py`
drives with synthetic `strings`/`nm` output.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

FORBIDDEN_BINARY_STRINGS = [
    "godotDelegate",
    "ENetGodotSocket",
    "GDScriptInternal",
    "godot_packHalf2x16",
    "godot_unpackHalf2x16",
    "FFX_FSR2_OPTION_GODOT",
    "icu_78_godot",
    "godot_init_profiler",
    "godot_unzip_",
    "godot_open",
    "godot_read",
    "godot_write",
    "godot_unpackUnorm4x8",
    "org.godotengine.godot.popup_window",
    "Waiting for Godot",
    "godotPoolSize",
    "threads/godot_pool_size",
    "user://logs/godot.log",
    "Godot mobile VR interface",
    "Main Godot OpenXR",
    "Godot action set",
    "Open Godot online documentation",
    "Godot Shading Language",
    "Godot binary",
    "compiling Godot",
    "Godot Engine running with display/window/energy_saving/keep_screen_on = true",
    "Godot Engine/1.0",
    "cn=Godot, ou=Godot Engine",
    "Godot Engine:'docs'",
    "link to Godot Engine",
    'dictionary["name"] == "Godot Engine"',
    '{"name": "Godot Engine"',
    "Godot v",
    "Godot will start in normal mode",
]


def find_forbidden_tokens(scan_text: str) -> list[str]:
    """Return every forbidden token present in `scan_text`, in policy order."""
    return [token for token in FORBIDDEN_BINARY_STRINGS if token in scan_text]


def main() -> int:
    parser = argparse.ArgumentParser(description="Scan a Foundry binary for high-signal internal Godot spellings.")
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()

    if not args.binary.is_file():
        print(f"Binary not found: {args.binary}", file=sys.stderr)
        return 1

    strings = subprocess.run(["strings", "-a", str(args.binary)], check=True, text=True, stdout=subprocess.PIPE).stdout
    nm = subprocess.run(["nm", "-a", str(args.binary)], check=False, text=True, stdout=subprocess.PIPE).stdout
    scan_text = f"{strings}\n{nm}"
    leaked = find_forbidden_tokens(scan_text)
    if leaked:
        print(f"{args.binary} still contains forbidden binary spellings: {leaked}", file=sys.stderr)
        return 1

    print("Foundry binary naming check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
