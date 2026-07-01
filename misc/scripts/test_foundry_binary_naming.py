#!/usr/bin/env python3
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
    leaked = [token for token in FORBIDDEN_BINARY_STRINGS if token in scan_text]
    if leaked:
        print(f"{args.binary} still contains forbidden binary spellings: {leaked}", file=sys.stderr)
        return 1

    print("Foundry binary naming tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
