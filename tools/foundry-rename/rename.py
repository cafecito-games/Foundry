# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.
"""Foundry rename engine.

Applies a generated ``naming_map.tsv`` to the source tree. The engine is:

  * longest-match-first  -- ``GDScriptParser`` is rewritten to ``FSParser``
    before the shorter ``GDScript`` rule can corrupt it;
  * context-aware        -- ``--context code`` maps ``GDScript`` to
    ``FoundryScript``; ``--context prose`` maps it to ``Foundry Script``;
  * word-boundary-safe   -- identifier rules never touch ``GDScript`` inside
    ``MyGDScriptHelper`` unless that exact token is itself a rule;
  * literal for the rest -- tokens that start with ``.`` (file extensions) or
    contain a space (prose phrases) match literally, with extensions anchored
    to a word boundary so ``.gd`` does not eat ``.gdshader``;
  * exclusion-aware      -- any ``thirdparty/`` or ``.git/`` dir (nested too,
    e.g. ``modules/mono/thirdparty/**``) is never touched;
  * idempotent           -- a second run is a no-op (no ``to`` value re-triggers
    any rule);
  * dry-runnable         -- ``--dry-run`` writes nothing;
  * move-capable         -- file/dir renames go through ``git mv`` deepest-first.
"""

import argparse
import os
import re
import subprocess
import sys

from common import is_excluded, iter_tracked_files

HERE = os.path.dirname(os.path.abspath(__file__))

IDENTIFIER_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

# Contexts considered when planning file/dir moves. The prose mapping (e.g.
# "GDScript" -> "Foundry Script") must never rename a path, so it is excluded.
MOVE_CONTEXTS = ("code", "both")


def load_rules(path):
    """Parse a naming map TSV into a list of row dicts."""
    rows = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            rows.append(
                {
                    "from": fields[0],
                    "to": fields[1],
                    "category": fields[2] if len(fields) > 2 else "",
                    "context": fields[3] if len(fields) > 3 else "code",
                    "notes": fields[4] if len(fields) > 4 else "",
                }
            )
    return rows


def compile_rule(row):
    """Compile one rule into a ``(kind, pattern, to, src)`` tuple.

    ``kind`` is ``"regex"`` for identifier and file-extension rules and
    ``"literal"`` for everything else (prose phrases, dotted filenames, URLs).
    """
    src = row["from"]
    to = row["to"]
    if IDENTIFIER_RE.fullmatch(src):
        # An identifier rule whose source ends in "_" is a prefix rule: it
        # matches the leading run of a snake_case token (e.g. "gdscript_" in
        # "gdscript_parser") so a whole family of identifiers and include paths
        # can be renamed by one rule. The leading word boundary is still
        # enforced, so it never matches mid-token (e.g. "my_gdscript_path").
        trailing = "" if src.endswith("_") else r"(?![A-Za-z0-9_])"
        pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(src) + trailing)
        return ("regex", pattern, to, src)
    if src.startswith("."):
        # Treat a file extension as a token: anchor the trailing edge so ".gd"
        # matches ".gd" but never the start of ".gdshader" or ".gdextension".
        pattern = re.compile(re.escape(src) + r"(?![A-Za-z0-9_])")
        return ("regex", pattern, to, src)
    return ("literal", None, to, src)


def select_rules(rows, contexts):
    """Compile and longest-first sort the rules whose context is in ``contexts``."""
    chosen = [row for row in rows if row["context"] in contexts]
    chosen.sort(key=lambda row: len(row["from"]), reverse=True)
    return [compile_rule(row) for row in chosen]


def replace_text(text, compiled_rules):
    """Apply compiled rules to ``text`` in the given (longest-first) order.

    Sequential-substitution invariant: correctness and idempotence rely on no
    rule's ``from`` appearing inside any rule's ``to`` (e.g. ``FoundryScript``
    must not contain ``GDScript``). Because the naming map satisfies this, a
    later rule can never re-trigger on the output of an earlier one, so a second
    pass is a guaranteed no-op.
    """
    for kind, pattern, to, src in compiled_rules:
        if kind == "regex":
            text = pattern.sub(lambda match, to=to: to, text)
        else:
            text = text.replace(src, to)
    return text


def tracked_files(root):
    """List git-tracked files under ``root`` (excluding thirdparty/.git)."""
    return list(iter_tracked_files(root))


def plan_file_moves(paths, move_rules):
    """Return ``(src, dst)`` pairs for paths whose name changes, deepest-first."""
    moves = []
    for path in paths:
        if is_excluded(path):
            continue
        new_path = replace_text(path, move_rules)
        if new_path != path:
            moves.append((path, new_path))
    moves.sort(key=lambda pair: pair[0].count("/"), reverse=True)
    return moves


def run_content_pass(root, rules, contexts, targets, dry_run):
    """Rewrite the contents of ``targets`` using the rules for ``contexts``.

    Files are opened with ``newline=""`` for both read and write so existing
    line endings (e.g. the CRLF in ``.props``/``.sln``/``gradlew.bat``) survive
    untouched -- the rebrand must produce a clean mechanical diff, not flip line
    endings. Returns the number of files that changed (or would, when dry-run).
    """
    compiled = select_rules(rules, contexts)
    changed = 0
    for path in targets:
        if is_excluded(path):
            continue
        full = os.path.join(root, path)
        try:
            with open(full, "r", encoding="utf-8", newline="") as handle:
                text = handle.read()
        except (UnicodeDecodeError, OSError):
            continue
        new_text = replace_text(text, compiled)
        if new_text == text:
            continue
        changed += 1
        if dry_run:
            sys.stdout.write("would edit %s\n" % path)
        else:
            with open(full, "w", encoding="utf-8", newline="") as handle:
                handle.write(new_text)
            sys.stdout.write("edited %s\n" % path)
    sys.stderr.write("%d file(s) %s\n" % (changed, "would change" if dry_run else "changed"))
    return changed


def check_move_collisions(moves, root):
    """Raise ``ValueError`` if any planned move would clobber another path.

    Guards against two destinations colliding with each other, and against a
    destination that already exists and is not itself being moved away. Called
    before any ``git mv`` so a bad plan aborts before mutating the tree.
    """
    sources = {src for src, _ in moves}
    seen = {}
    for src, dst in moves:
        if dst in seen:
            raise ValueError("move collision: %r and %r both map to %r" % (seen[dst], src, dst))
        seen[dst] = src
        if os.path.exists(os.path.join(root, dst)) and dst not in sources:
            raise ValueError("move target already exists: %r -> %r" % (src, dst))


def run_move_pass(root, rules, dry_run):
    """Rename files/dirs via ``git mv``, deepest paths first, after a collision
    pre-flight that aborts the whole pass before any mutation if a target would
    be clobbered."""
    compiled = select_rules(rules, MOVE_CONTEXTS)
    moves = plan_file_moves(tracked_files(root), compiled)
    check_move_collisions(moves, root)
    for src, dst in moves:
        if dry_run:
            sys.stdout.write("git mv %s %s\n" % (src, dst))
            continue
        dst_dir = os.path.dirname(os.path.join(root, dst))
        if dst_dir:
            os.makedirs(dst_dir, exist_ok=True)
        subprocess.check_call(["git", "mv", src, dst], cwd=root)
    sys.stderr.write("%d move(s) %s\n" % (len(moves), "planned" if dry_run else "applied"))
    return moves


def main(argv=None):
    parser = argparse.ArgumentParser(description="Apply the Foundry naming map.")
    parser.add_argument(
        "--map", default=os.path.join(HERE, "naming_map.tsv"), help="path to naming_map.tsv (default: ./naming_map.tsv)"
    )
    parser.add_argument("--root", default=None, help="repository root (default: auto-detected)")
    parser.add_argument(
        "--context", choices=("code", "prose"), default="code", help="replacement context for the content pass"
    )
    parser.add_argument("--content", action="store_true", help="rewrite file contents")
    parser.add_argument("--moves", action="store_true", help="rename files/dirs with git mv")
    parser.add_argument("--dry-run", action="store_true", help="write nothing; print the planned actions")
    parser.add_argument("paths", nargs="*", help="files for the content pass (default: all tracked)")
    args = parser.parse_args(argv)

    if not args.content and not args.moves:
        parser.error("pass --content and/or --moves")

    root = args.root or find_repo_root(HERE)
    rules = load_rules(args.map)

    if args.content:
        # The "both" context rules (file extensions, URLs) always apply on top
        # of the selected code/prose rules.
        contexts = (args.context, "both")
        targets = args.paths if args.paths else tracked_files(root)
        run_content_pass(root, rules, contexts, targets, args.dry_run)

    if args.moves:
        run_move_pass(root, rules, args.dry_run)

    return 0


def find_repo_root(start):
    path = os.path.abspath(start)
    while True:
        marker = os.path.join(path, ".git")
        if os.path.isdir(marker) or os.path.isfile(marker):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            return os.path.abspath(start)
        path = parent


if __name__ == "__main__":
    raise SystemExit(main())
