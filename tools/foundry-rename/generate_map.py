# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.
"""Generate the Foundry naming map.

Scans the frozen Godot source tree for the families of identifiers that the
Foundry rebrand renames (GDScript*, GDExtension*, the GD* object macros and the
GODOT_* macro/define family), turns each discovered identifier into a concrete
``from -> to`` row, then merges the hand-curated ``seed.tsv`` on top of the
generated rows.

The merge rules:
  * Generated rows always carry ``context = code`` (they are C/C++/Python
    identifiers).
  * Seed rows infer their context (``code`` / ``prose`` / ``both``) from their
    shape (see ``seed_context``); the prose ``GDScript -> Foundry Script`` rule
    is therefore *additive* and coexists with the generated code rule.
  * On a conflict for the same ``(from, context)`` pair, the SEED wins.
  * A seed row whose ``to`` is the literal ``!EXCLUDE`` removes every row (any
    context) with that ``from`` from the output.

Output is a TSV sorted longest-source-first so the replacement engine can apply
it deterministically.
"""

import argparse
import os
import re
import sys

from common import SOURCE_EXTENSIONS, SOURCE_FILENAMES, is_source_file, iter_tracked_files, toolkit_prefix

# Re-exported for callers/tests that reference these via generate_map; the
# canonical definitions live in common so the scan and rename passes can never
# disagree on scope.
__all__ = ["SOURCE_EXTENSIONS", "SOURCE_FILENAMES", "is_source_file", "toolkit_prefix"]

HERE = os.path.dirname(os.path.abspath(__file__))

EXCLUDE_MARKER = "!EXCLUDE"

OUTPUT_COLUMNS = ("from", "to", "category", "context", "notes")

# Object-system macros with bespoke spellings (no clean prefix swap).
MACROS = {
    "GDCLASS": "FOUNDRY_CLASS",
    "GDSOFTCLASS": "FOUNDRY_SOFTCLASS",
}

# A single identifier-shaped token belonging to one of the rename families.
# Greedy suffix classes consume the whole identifier; the surrounding negative
# look-around makes every alternative match a complete word so we never rewrite
# a fragment of a larger identifier (e.g. the GDScript inside MyGDScriptHelper).
TOKEN_RE = re.compile(
    r"(?<![A-Za-z0-9_])"
    r"(?:"
    r"GDScript[A-Za-z0-9_]*"
    r"|GDExtension[A-Za-z0-9_]*"
    r"|GDVIRTUAL[A-Za-z0-9_]*"
    r"|GDREGISTER_[A-Za-z0-9_]+"
    r"|GDSOFTCLASS"
    r"|GDCLASS"
    r"|GODOT_[A-Za-z0-9_]+"
    r")"
    r"(?![A-Za-z0-9_])"
)


def classify_token(token):
    """Map a discovered token to its replacement and rename category.

    Categories: A = GDScript family, B = GDExtension family, C = GD* macros,
    D = GODOT_* family. Raises ``ValueError`` for anything that is not a member
    of a known family (which should never happen for a ``TOKEN_RE`` match).
    """
    if token == "GDScript":
        return ("FoundryScript", "A")
    if token.startswith("GDScript"):
        return ("FS" + token[len("GDScript") :], "A")
    if token == "GDExtension":
        return ("FoundryExtension", "B")
    if token.startswith("GDExtension"):
        return ("FoundryExtension" + token[len("GDExtension") :], "B")
    if token in MACROS:
        return (MACROS[token], "C")
    if token.startswith("GDVIRTUAL"):
        return ("FOUNDRY_VIRTUAL" + token[len("GDVIRTUAL") :], "C")
    if token.startswith("GDREGISTER_"):
        return ("FOUNDRY_REGISTER_" + token[len("GDREGISTER_") :], "C")
    if token.startswith("GODOT_"):
        return ("FOUNDRY_" + token[len("GODOT_") :], "D")
    raise ValueError("unclassifiable token: %r" % (token,))


def scan_text(text):
    """Return the set of rename-family tokens present in ``text``."""
    return set(TOKEN_RE.findall(text))


def tracked_files(root):
    """List git-tracked files under ``root``, excluding thirdparty/.git.

    Delegates to the shared ``iter_tracked_files`` so the generated map and the
    rename pass share one definition of scope.
    """
    return iter_tracked_files(root)


def scan_tokens(root):
    """Scan all source files under ``root`` and return the discovered tokens."""
    tokens = set()
    skip_prefix = toolkit_prefix(root)
    for path in tracked_files(root):
        if skip_prefix and path.startswith(skip_prefix):
            continue
        if not is_source_file(path):
            continue
        full = os.path.join(root, path)
        try:
            with open(full, "r", encoding="utf-8") as handle:
                text = handle.read()
        except (UnicodeDecodeError, OSError):
            # Skip binaries or unreadable files; tokens are ASCII identifiers
            # and always live in decodable UTF-8 source.
            continue
        tokens |= scan_text(text)
    return tokens


def build_generated_rows(tokens):
    """Turn discovered tokens into generated (context=code) rows."""
    rows = []
    for token in sorted(tokens):
        to, category = classify_token(token)
        rows.append(
            {
                "from": token,
                "to": to,
                "category": category,
                "context": "code",
                "notes": "generated",
            }
        )
    return rows


def seed_context(from_token, to_token):
    """Infer the replacement context for a seed row from its shape."""
    if " " in from_token or " " in to_token:
        return "prose"
    # File-extension / filename / URL rules apply to both code and prose.
    if from_token.startswith(".") or "." in from_token:
        return "both"
    return "code"


def load_seed(path):
    """Parse a seed TSV into row dicts with an inferred context."""
    rows = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            from_token = fields[0]
            to_token = fields[1]
            category = fields[2] if len(fields) > 2 and fields[2] else ""
            notes = fields[3] if len(fields) > 3 else ""
            rows.append(
                {
                    "from": from_token,
                    "to": to_token,
                    "category": category,
                    "context": seed_context(from_token, to_token),
                    "notes": notes,
                }
            )
    return rows


def merge_seed(generated_rows, seed_rows):
    """Merge seed rows over generated rows.

    Seed wins on a ``(from, context)`` collision; a seed ``to`` of
    ``!EXCLUDE`` drops every row (all contexts) sharing that ``from``.
    """
    merged = {}
    for row in generated_rows:
        merged[(row["from"], row["context"])] = row

    excluded = set()
    for row in seed_rows:
        if row["to"] == EXCLUDE_MARKER:
            excluded.add(row["from"])
            continue
        merged[(row["from"], row["context"])] = row

    for key in list(merged):
        if key[0] in excluded:
            del merged[key]

    return sort_rows(merged.values())


def sort_rows(rows):
    """Longest source first, then alphabetical for a stable, diffable order."""
    return sorted(rows, key=lambda r: (-len(r["from"]), r["from"], r["context"]))


def format_rows(rows):
    lines = [
        "# Foundry naming map (generated by generate_map.py).",
        "# Do not edit by hand: edit seed.tsv and regenerate.",
        "# Applied LONGEST-MATCH-FIRST. Excludes thirdparty/** and .git/**.",
        "#",
        "# " + "\t".join(OUTPUT_COLUMNS),
    ]
    for row in rows:
        # rstrip so rows with an empty trailing "notes" field carry no trailing
        # tab (matches the repo's file-format hook, keeping the map idempotent).
        line = "\t".join([row["from"], row["to"], row["category"], row["context"], row["notes"]])
        lines.append(line.rstrip())
    return "\n".join(lines) + "\n"


def generate(root, seed_path):
    tokens = scan_tokens(root)
    generated = build_generated_rows(tokens)
    seed = load_seed(seed_path)
    return merge_seed(generated, seed)


def find_repo_root(start):
    path = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(path, ".git")) or os.path.isfile(os.path.join(path, ".git")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            return os.path.abspath(start)
        path = parent


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate the Foundry naming map.")
    parser.add_argument(
        "--root",
        default=None,
        help="repository root to scan (default: auto-detected from this script)",
    )
    parser.add_argument(
        "--seed",
        default=os.path.join(HERE, "seed.tsv"),
        help="path to the seed TSV (default: ./seed.tsv)",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="write the map to this path (default: stdout)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="write nothing; print the map to stdout",
    )
    args = parser.parse_args(argv)

    root = args.root or find_repo_root(HERE)
    rows = generate(root, args.seed)
    text = format_rows(rows)

    if args.out and not args.dry_run:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
        sys.stderr.write("wrote %d rows to %s\n" % (len(rows), args.out))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
