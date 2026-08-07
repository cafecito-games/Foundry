#!/usr/bin/env python3
"""Verify every module and platform `get_doc_classes()` matches its shipped doc XML.

`SConstruct` builds the doctool save-path map from `get_doc_classes()` alone — from
`modules/<name>/config.py` and from `platform/<name>/detect.py` — so a class whose
XML ships in a `doc_classes/` directory but is absent from the matching list is
written back to `doc/classes/` on the next full documentation regeneration,
silently relocating the file. The inverse drift (a listed name with no file) is
harmless but signals a stale list.

Class names must be spelled exactly as the doc file basename, which for a
namespaced class is its qualified name (for example `foundry.http.server.HTTPServer`).

The registration lists are read with `ast` rather than imported: a build
configuration file pulls in SCons and repository-root helpers that are not
available inside a `pre-commit` hook environment, and importing it would run
arbitrary configuration code. Every list in the tree is a literal, and a
non-literal one is reported instead of silently skipped.

The mismatch logic lives in `find_mismatches`, which
`misc/checks/tests/test_check_doc_class_registration.py` drives with synthetic
component descriptions.
"""

from __future__ import annotations

import argparse
import ast
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

# Every tree `SConstruct` feeds into `env.doc_class_path`, and the file it reads
# `get_doc_classes()` from in each: modules use `config.py`, platforms `detect.py`.
REGISTRATION_SOURCES = (("modules", "config.py"), ("platform", "detect.py"))

DOC_CLASSES_FUNCTION = "get_doc_classes"
DOC_PATH_FUNCTION = "get_doc_path"


class UnreadableRegistration(Exception):
    """Raised when a registration list cannot be resolved without executing the file."""


def find_mismatches(component: str, listed_classes: list[str], present_files: list[str]) -> list[str]:
    """Return one message per registration mismatch for a single component."""
    listed = set(listed_classes)
    present = set(present_files)
    messages = []
    for class_name in sorted(present - listed):
        messages.append(
            f"{component}: '{class_name}' has a doc_classes XML file but is missing from "
            f"get_doc_classes(); a full documentation regeneration would relocate it to doc/classes/."
        )
    for class_name in sorted(listed - present):
        messages.append(
            f"{component}: '{class_name}' is listed in get_doc_classes() but has no "
            f"doc_classes XML file; remove the stale entry."
        )
    return messages


def read_returned_literal(source: str, function_name: str):
    """Return the literal a top-level function returns, or `None` if it is not defined."""
    tree = ast.parse(source)
    definition = None
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name == function_name:
            definition = node
    if definition is None:
        return None
    returns = [node for node in ast.walk(definition) if isinstance(node, ast.Return)]
    if len(returns) != 1 or returns[0].value is None:
        raise UnreadableRegistration(f"{function_name}() does not have exactly one literal return")
    try:
        return ast.literal_eval(returns[0].value)
    except ValueError as error:
        raise UnreadableRegistration(f"{function_name}() does not return a literal: {error}") from error


def check_component(component: str, source_path: Path) -> list[str]:
    """Return every registration mismatch for one module or platform directory."""
    present_files = sorted(path.stem for path in (source_path.parent / "doc_classes").glob("*.xml"))
    try:
        source = source_path.read_text()
        listed_classes = read_returned_literal(source, DOC_CLASSES_FUNCTION)
        doc_path = read_returned_literal(source, DOC_PATH_FUNCTION)
    except (OSError, SyntaxError, UnreadableRegistration) as error:
        return [f"{component}: cannot read doc class registration from {source_path.name}: {error}"]

    if listed_classes is None:
        if present_files:
            return [f"{component}: ships doc_classes XML files but defines no {DOC_CLASSES_FUNCTION}()."]
        return []

    if doc_path is None:
        return [
            f"{component}: defines {DOC_CLASSES_FUNCTION}() but no {DOC_PATH_FUNCTION}(); "
            f"the build registers no documentation path for it."
        ]

    if doc_path != "doc_classes":
        return [
            f"{component}: {DOC_PATH_FUNCTION}() returns '{doc_path}'; this check only "
            f"understands the conventional 'doc_classes' directory."
        ]

    return find_mismatches(component, list(listed_classes), present_files)


def check_repository(repository_root: Path) -> list[str]:
    """Return every registration mismatch across all modules and platforms."""
    messages = []
    for tree, source_name in REGISTRATION_SOURCES:
        for source_path in sorted((repository_root / tree).glob(f"*/{source_name}")):
            messages.extend(check_component(f"{tree}/{source_path.parent.name}", source_path))
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=REPOSITORY_ROOT,
        help="Repository root containing the modules/ and platform/ trees.",
    )
    parser.add_argument("files", nargs="*", help="Ignored; accepted so pre-commit can pass file names.")
    arguments = parser.parse_args()

    messages = check_repository(arguments.repository_root)
    for message in messages:
        print(message, file=sys.stderr)
    if messages:
        print(f"{len(messages)} doc class registration mismatch(es) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
