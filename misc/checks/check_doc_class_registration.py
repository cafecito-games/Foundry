#!/usr/bin/env python3
"""Verify every module's `get_doc_classes()` matches its shipped doc XML files.

`SConstruct` builds the doctool save-path map from `get_doc_classes()` alone, so a
class whose XML ships in `modules/<name>/doc_classes/` but is absent from the list
is written back to `doc/classes/` on the next full documentation regeneration,
silently relocating the file. The inverse drift (a listed name with no file) is
harmless but signals a stale list.

Class names must be spelled exactly as the doc file basename, which for a
namespaced class is its qualified name (for example `foundry.http.server.HTTPServer`).

The mismatch logic lives in `find_mismatches`, which
`misc/checks/tests/test_check_doc_class_registration.py` drives with synthetic
module descriptions.
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


def find_mismatches(module_name: str, listed_classes: list[str], present_files: list[str]) -> list[str]:
    """Return one message per registration mismatch for a single module."""
    listed = set(listed_classes)
    present = set(present_files)
    messages = []
    for class_name in sorted(present - listed):
        messages.append(
            f"modules/{module_name}: '{class_name}' has a doc_classes XML file but is missing from "
            f"get_doc_classes(); a full documentation regeneration would relocate it to doc/classes/."
        )
    for class_name in sorted(listed - present):
        messages.append(
            f"modules/{module_name}: '{class_name}' is listed in get_doc_classes() but has no "
            f"doc_classes XML file; remove the stale entry."
        )
    return messages


def load_module_config(config_path: Path):
    """Import a module `config.py` the way `SConstruct` does, under a unique name."""
    module_name = f"foundry_module_config_{config_path.parent.name}"
    spec = importlib.util.spec_from_file_location(module_name, config_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load {config_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_repository(modules_root: Path) -> list[str]:
    """Return every registration mismatch across all modules under `modules_root`."""
    messages = []
    for config_path in sorted(modules_root.glob("*/config.py")):
        module_name = config_path.parent.name
        try:
            config = load_module_config(config_path)
        except Exception as error:  # noqa: BLE001 - reported, not raised, so all modules are checked.
            messages.append(f"modules/{module_name}: cannot import config.py: {error}")
            continue

        get_doc_classes = getattr(config, "get_doc_classes", None)
        get_doc_path = getattr(config, "get_doc_path", None)
        doc_directory = config_path.parent / "doc_classes"
        present_files = sorted(path.stem for path in doc_directory.glob("*.xml"))

        if get_doc_classes is None:
            if present_files:
                messages.append(f"modules/{module_name}: ships doc_classes XML files but defines no get_doc_classes().")
            continue

        if get_doc_path is None:
            messages.append(
                f"modules/{module_name}: defines get_doc_classes() but no get_doc_path(); "
                f"the build registers no documentation path for it."
            )
            continue

        doc_path = get_doc_path()
        if doc_path != "doc_classes":
            messages.append(
                f"modules/{module_name}: get_doc_path() returns '{doc_path}'; this check only "
                f"understands the conventional 'doc_classes' directory."
            )
            continue

        messages.extend(find_mismatches(module_name, list(get_doc_classes()), present_files))
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--modules-root",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "modules",
        help="Directory containing the engine modules (defaults to the repository's modules/).",
    )
    parser.add_argument("files", nargs="*", help="Ignored; accepted so pre-commit can pass file names.")
    arguments = parser.parse_args()

    messages = check_repository(arguments.modules_root)
    for message in messages:
        print(message, file=sys.stderr)
    if messages:
        print(f"{len(messages)} doc class registration mismatch(es) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
