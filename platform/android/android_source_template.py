#!/usr/bin/env python3
"""Inspect and atomically promote Foundry's self-contained Android source template."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

EXPECTED_AARS = frozenset(
    {
        "libs/debug/foundry-debug.aar",
        "libs/dev/foundry-dev.aar",
        "libs/release/foundry-release.aar",
    }
)
REQUIRED_FILES = frozenset(
    {
        "build.gradle",
        "config.gradle",
        "gradlew",
        "gradle/wrapper/gradle-wrapper.jar",
        "gradle/wrapper/gradle-wrapper.properties",
        "src/main/AndroidManifest.xml",
        *EXPECTED_AARS,
    }
)
APP_SOURCE_PATTERN = re.compile(
    r"^src/(?:main|instrumented|androidTestInstrumented)/"
    r"(?:java|kotlin|aidl)/games/cafecito/foundry/game/"
)
SOURCE_SUFFIXES = frozenset({".aidl", ".java", ".kt"})
FORBIDDEN_PREFIXES = (
    "app/",
    "lib/",
    "platform/android/java/lib/",
    "runtime/",
)


class SourceTemplateError(RuntimeError):
    """A malformed or ownership-violating Android source template."""


def _normalized_entry(info: zipfile.ZipInfo) -> str:
    raw = info.filename
    if not raw or "\\" in raw or raw.startswith("/"):
        raise SourceTemplateError(f"source template has unsafe archive entry: {raw!r}")
    name = raw[:-1] if info.is_dir() else raw
    path = PurePosixPath(name)
    if not name or ".." in path.parts or "." in path.parts or path.as_posix() != name:
        raise SourceTemplateError(f"source template has non-normalized archive entry: {raw!r}")
    mode = (info.external_attr >> 16) & 0xFFFF
    if mode and stat.S_ISLNK(mode):
        raise SourceTemplateError(f"source template contains a symbolic link: {raw!r}")
    if info.flag_bits & 0x1:
        raise SourceTemplateError(f"source template contains an encrypted entry: {raw!r}")
    return name


def inspect_source_template(archive_path: Path) -> tuple[str, ...]:
    """Validate the final source ZIP and return its sorted regular-file entries."""

    archive_path = archive_path.absolute()
    if archive_path.is_symlink() or not archive_path.is_file():
        raise SourceTemplateError(f"Android source template must be a regular file: {archive_path}")
    archive_path = archive_path.resolve()
    try:
        archive = zipfile.ZipFile(archive_path)
    except (OSError, zipfile.BadZipFile) as error:
        raise SourceTemplateError(f"unable to open Android source template {archive_path}: {error}") from error

    with archive:
        files: set[str] = set()
        entries: set[str] = set()
        for info in archive.infolist():
            name = _normalized_entry(info)
            if name in entries:
                raise SourceTemplateError(f"source template contains duplicate entry: {name}")
            entries.add(name)
            if info.is_dir():
                continue
            files.add(name)

            if name.startswith(FORBIDDEN_PREFIXES):
                raise SourceTemplateError(f"source template contains a forbidden in-tree runtime path: {name}")
            if PurePosixPath(name).suffix in SOURCE_SUFFIXES and APP_SOURCE_PATTERN.match(name) is None:
                raise SourceTemplateError(f"source template contains runtime source outside the app package: {name}")
            if name.endswith(".aar") and name not in EXPECTED_AARS:
                raise SourceTemplateError(f"source template contains an unexpected AAR: {name}")
            if name in EXPECTED_AARS and info.file_size == 0:
                raise SourceTemplateError(f"source template contains an empty in-tree host AAR: {name}")

        missing = sorted(REQUIRED_FILES - files)
        if missing:
            raise SourceTemplateError(f"source template is missing required entry: {missing[0]}")
        actual_aars = {name for name in files if name.endswith(".aar")}
        if actual_aars != EXPECTED_AARS:
            raise SourceTemplateError(
                f"source template AAR set mismatch: expected {sorted(EXPECTED_AARS)!r}, got {sorted(actual_aars)!r}"
            )
        return tuple(sorted(files))


def promote_source_template(archive_path: Path, destination: Path) -> tuple[str, ...]:
    """Copy a staged ZIP, validate those exact bytes, and atomically promote it."""

    archive_path = archive_path.absolute()
    if archive_path.is_symlink() or not archive_path.is_file():
        raise SourceTemplateError(f"Android source template must be a regular file: {archive_path}")
    archive_path = archive_path.resolve()
    destination = destination.absolute()
    if archive_path == destination.resolve():
        raise SourceTemplateError("staged Android source template and destination must be different paths")
    if destination.is_dir():
        raise SourceTemplateError(f"Android source template destination is a directory: {destination}")
    if destination.exists() or destination.is_symlink():
        destination.unlink()

    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}-",
        suffix=".tmp",
        dir=destination.parent,
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        shutil.copyfile(archive_path, temporary)
        names = inspect_source_template(temporary)
        os.replace(temporary, destination)
    except OSError as error:
        raise SourceTemplateError(f"unable to promote Android source template to {destination}: {error}") from error
    finally:
        temporary.unlink(missing_ok=True)
    return names


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect or promote Foundry's Android source template ZIP.")
    commands = parser.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("inspect", help="validate one final Android source template")
    inspect.add_argument("--archive", required=True, type=Path)
    promote = commands.add_parser("promote", help="validate and atomically promote one staged source template")
    promote.add_argument("--archive", required=True, type=Path)
    promote.add_argument("--destination", required=True, type=Path)
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    try:
        if arguments.command == "inspect":
            names = inspect_source_template(arguments.archive)
        else:
            names = promote_source_template(arguments.archive, arguments.destination)
    except SourceTemplateError as error:
        print(f"Android source template validation failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps({"entries": list(names)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
