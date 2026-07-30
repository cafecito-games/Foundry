#!/usr/bin/env python3
"""Evaluate the declarative file-content policy in `misc/checks/file_policy.toml`.

The policy expresses exactly four primitives: a literal token must appear in the
files a glob matches, must not appear, a path must exist, or must not exist.
Anything that cannot be said that way is not policy — it wants a real test.

The decision logic is pure: `load_rules` turns TOML text into rules and
`find_policy_violations` evaluates them against a directory tree, both driven by
`misc/checks/tests/test_check_file_policy.py` on synthetic fixture trees.

A glob that matches no files is a violation, not a pass. A rule nobody's paths
reach is decoration, and decoration is worse than no rule at all: it reads as
coverage the repository does not have.
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

DEFAULT_POLICY = Path(__file__).resolve().parent / "file_policy.toml"
DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Generated trees mirror real source paths, so scanning them would make the
# policy depend on whether someone happened to build locally.
SKIPPED_DIRECTORY_NAMES = frozenset(
    {
        ".git",
        ".gradle",
        ".kotlin",
        ".test_scratch",
        ".venv",
        ".worktrees",
        "__pycache__",
        "build",
        "node_modules",
    }
)

TOKEN_KEYS = ("required", "forbidden")
PATH_KEYS = ("required_paths", "forbidden_paths")
KNOWN_KEYS = frozenset({"name", "reason", "paths", *TOKEN_KEYS, *PATH_KEYS})


class FilePolicyError(RuntimeError):
    """The policy table itself is malformed and cannot be evaluated."""


@dataclass(frozen=True)
class Rule:
    name: str
    reason: str
    paths: tuple[str, ...] = ()
    required: tuple[str, ...] = ()
    forbidden: tuple[str, ...] = ()
    required_paths: tuple[str, ...] = ()
    forbidden_paths: tuple[str, ...] = ()


def _string_list(rule_name: str, key: str, value: Any) -> tuple[str, ...]:
    if not isinstance(value, list) or not value:
        raise FilePolicyError(f"rule {rule_name!r}: {key} must be a non-empty list of strings")
    for entry in value:
        if not isinstance(entry, str) or not entry.strip():
            raise FilePolicyError(f"rule {rule_name!r}: {key} must contain only non-empty strings")
    return tuple(value)


def load_rules(policy_text: str) -> list[Rule]:
    """Parse the policy table, rejecting anything the runner cannot evaluate."""

    try:
        document = tomllib.loads(policy_text)
    except tomllib.TOMLDecodeError as error:
        raise FilePolicyError(f"the policy is not valid TOML: {error}") from error

    entries = document.get("rule")
    if not isinstance(entries, list) or not entries:
        raise FilePolicyError("the policy must declare at least one [[rule]] entry")

    rules: list[Rule] = []
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise FilePolicyError(f"rule #{index + 1} is not a table")

        name = entry.get("name")
        if not isinstance(name, str) or not name.strip():
            raise FilePolicyError(f"rule #{index + 1} has no name")
        if name in seen:
            raise FilePolicyError(f"rule {name!r}: duplicate rule name")
        seen.add(name)

        unknown = sorted(set(entry) - KNOWN_KEYS)
        if unknown:
            raise FilePolicyError(
                f"rule {name!r}: unknown key(s) {', '.join(unknown)}; the schema is deliberately "
                "limited to literal token and path lists"
            )

        reason = entry.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            raise FilePolicyError(f"rule {name!r}: every rule needs a non-empty reason")

        tokens = {key: _string_list(name, key, entry[key]) for key in TOKEN_KEYS if key in entry}
        paths = {key: _string_list(name, key, entry[key]) for key in PATH_KEYS if key in entry}
        if not tokens and not paths:
            raise FilePolicyError(f"rule {name!r}: asserts nothing")

        patterns: tuple[str, ...] = ()
        if "paths" in entry:
            patterns = _string_list(name, "paths", entry["paths"])
        elif tokens:
            raise FilePolicyError(f"rule {name!r}: token rules need paths to look in")

        rules.append(
            Rule(
                name=name,
                reason=reason.strip(),
                paths=patterns,
                required=tokens.get("required", ()),
                forbidden=tokens.get("forbidden", ()),
                required_paths=paths.get("required_paths", ()),
                forbidden_paths=paths.get("forbidden_paths", ()),
            )
        )
    return rules


def _is_scannable(root: Path, path: Path) -> bool:
    # Only the part of the path below `root` decides: the tree may itself live
    # under a directory whose name is skipped, such as a `.worktrees` checkout.
    return path.is_file() and not SKIPPED_DIRECTORY_NAMES.intersection(path.relative_to(root).parts)


def _matching_files(root: Path, pattern: str) -> list[Path]:
    return sorted(path for path in root.glob(pattern) if _is_scannable(root, path))


def _report(root: Path, path: Path | str, rule: Rule, detail: str) -> str:
    relative = path.relative_to(root).as_posix() if isinstance(path, Path) else path
    return f"{relative}: {rule.name}: {detail}: {rule.reason}"


def find_policy_violations(rules: Sequence[Rule], root: Path) -> list[str]:
    """Return every violation of *rules* under *root*, never stopping at the first."""

    violations: list[str] = []
    for rule in rules:
        for pattern in rule.paths:
            matches = _matching_files(root, pattern)
            if not matches:
                violations.append(
                    f"{pattern}: {rule.name}: path pattern matched no files, so the rule "
                    f"asserts nothing: {rule.reason}"
                )
                continue
            for path in matches:
                contents = path.read_text(encoding="utf-8", errors="replace")
                for token in rule.required:
                    if token not in contents:
                        violations.append(_report(root, path, rule, f"required literal {token!r} is missing"))
                for token in rule.forbidden:
                    if token in contents:
                        violations.append(_report(root, path, rule, f"forbidden literal {token!r} is present"))

        for relative in rule.required_paths:
            if not (root / relative).exists():
                violations.append(_report(root, relative, rule, "required path is missing"))
        for relative in rule.forbidden_paths:
            if (root / relative).exists():
                violations.append(_report(root, relative, rule, "removed path still exists"))
    return violations


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Evaluate the declarative Foundry file-content policy.")
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY, help="policy table to evaluate")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="tree the policy applies to")
    args = parser.parse_args(argv)

    rules = load_rules(args.policy.read_text(encoding="utf-8"))
    violations = find_policy_violations(rules, args.root.resolve())
    if violations:
        print(f"File policy failed ({len(violations)} violations):", file=sys.stderr)
        for violation in violations:
            print(f"- {violation}", file=sys.stderr)
        return 1

    print(f"File policy passed ({len(rules)} rules).")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FilePolicyError as error:
        print(f"misc/checks/file_policy.toml is unusable: {error}", file=sys.stderr)
        sys.exit(2)
