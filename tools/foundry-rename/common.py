# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.
"""Shared path/scope helpers for the Foundry rename toolkit.

``generate_map.py`` (which decides *what* to rename) and ``rename.py`` (which
*applies* the rename) must agree exactly on which files are in scope; otherwise
the generated map could reference tokens that the rename pass never visits, or
vice versa. Both import the single ``is_excluded`` / ``iter_tracked_files`` pair
defined here so their notion of scope can never drift apart.
"""

import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))

# Source-code file types that may legitimately *define* one of the tokens.
# Prose/translation files (.po, .md, .xml docs, ...) are intentionally excluded:
# they only ever reference tokens already discovered in code, and they introduce
# false tokens such as the Czech declension "GDScriptu" in editor .po catalogs.
# The content pass uses the same set so it never rewrites prose/translation
# surfaces, which are handled separately in the prose context.
SOURCE_EXTENSIONS = {
    ".h",
    ".hpp",
    ".hh",
    ".hxx",
    ".inc",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".m",
    ".mm",
    ".py",
    ".pyi",
    ".java",
    ".kt",
    ".kts",
    ".js",
    ".cjs",
    ".mjs",
    ".glsl",
    ".json",
    ".gradle",
    ".yml",
    ".yaml",
    ".rc",
    ".props",
    ".targets",
    ".plist",
    ".sh",
    ".fish",
    ".template",
}
SOURCE_FILENAMES = {"SConstruct", "SCsub"}


def is_source_file(path):
    """True if ``path`` is an engine source file (vs. prose/translation/data)."""
    base = os.path.basename(path)
    if base in SOURCE_FILENAMES:
        return True
    return os.path.splitext(base)[1] in SOURCE_EXTENSIONS


def toolkit_prefix(root):
    """Path prefix of this toolkit, relative to ``root`` (or None if outside).

    The toolkit's own sources and test fixtures contain example tokens (e.g. the
    Czech declension "GDScriptu"); they must never be scanned or rewritten as
    engine source.
    """
    rel = os.path.relpath(HERE, root).replace(os.sep, "/")
    if rel.startswith(".."):
        return None
    return rel.rstrip("/") + "/"


def is_excluded(path):
    """True if ``path`` lives anywhere under a ``thirdparty/`` or ``.git/`` dir.

    The check is on path-segment membership, not a prefix, so nested vendor
    trees such as ``modules/mono/thirdparty/**`` are excluded as well as the
    top-level ``thirdparty/``.
    """
    segments = path.replace("\\", "/").split("/")
    return "thirdparty" in segments or ".git" in segments


def iter_tracked_files(root):
    """Yield git-tracked file paths under ``root``, excluding thirdparty/.git.

    Filenames are decoded with ``surrogateescape`` so paths containing bytes
    that are not valid UTF-8 round-trip instead of raising.
    """
    output = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode("utf-8", errors="surrogateescape")
    for path in output.split("\0"):
        if path and not is_excluded(path):
            yield path
