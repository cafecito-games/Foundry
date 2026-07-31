#!/usr/bin/env python3
"""Check that the FoundryScript TextMate grammar has not drifted from the language.

Three descriptions of the keyword layer have to agree, and each one can move on its own:

* the tokenizer `KEYWORDS` table, which is what the engine actually lexes;
* `modules/foundry_script/GRAMMAR.md` section 2.5, which is the normative specification;
* the generated TextMate grammar, which is what editors highlight with.

Section 2.5 deliberately keeps three sets apart -- reserved words, the built-in numeric
constants, and contextual keywords that stay ordinary identifiers -- so this check
compares them as three separate sets. Folding them into one keyword list would let a
contextual keyword silently become globally reserved in editors.

`parse_specification_keywords`, `grammar_keyword_words`, and `find_violations` are pure
and are driven by `misc/checks/tests/test_check_foundryscript_tmlanguage.py`.
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path
from typing import Any, NamedTuple

REPO_ROOT = Path(__file__).resolve().parents[2]
GRAMMAR_DIRECTORY = REPO_ROOT / "modules/foundry_script/grammar"
DEFAULT_BUILDER = GRAMMAR_DIRECTORY / "tmlanguage_builder.py"
DEFAULT_PATTERNS_DIRECTORY = GRAMMAR_DIRECTORY / "patterns"
DEFAULT_TOKENIZER = REPO_ROOT / "modules/foundry_script/fs_tokenizer.cpp"
DEFAULT_SPECIFICATION = REPO_ROOT / "modules/foundry_script/GRAMMAR.md"

KEYWORD_SECTION = re.compile(r"^### 2\.5 Keywords\s*$", re.MULTILINE)
NEXT_SECTION = re.compile(r"^### 2\.6 ", re.MULTILINE)
FENCED_BLOCK = re.compile(r"^```\s*\n(.*?)^```\s*$", re.DOTALL | re.MULTILINE)
NUMERIC_CONSTANT_PARAGRAPH = re.compile(r"^Built-in numeric constants\b.*?(?=\n\s*\n)", re.DOTALL | re.MULTILINE)
CONTEXTUAL_HEADING = re.compile(r"^\*\*Contextual keywords\*\*", re.MULTILINE)
BULLET = re.compile(r"^- (.*)$", re.MULTILINE)
BACKTICKED = re.compile(r"`([^`]+)`")
ALTERNATION = re.compile(r"^\\b\(\?:(.+)\)\\b$")


class SpecificationKeywords(NamedTuple):
    """The three keyword sets `GRAMMAR.md` section 2.5 documents."""

    reserved: frozenset[str]
    numeric_constants: frozenset[str]
    contextual: frozenset[str]


class GrammarWords(NamedTuple):
    """The word sets the generated TextMate grammar actually highlights."""

    reserved: frozenset[str]
    numeric_constants: frozenset[str]
    literal_constants: frozenset[str]


def load_builder(path: Path = DEFAULT_BUILDER) -> Any:
    """Import the grammar generator from its path, since `modules/` is not a package."""

    specification = importlib.util.spec_from_file_location("tmlanguage_builder", path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot import the grammar generator from {path}")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def keyword_section(markdown: str) -> str:
    """Return the body of `GRAMMAR.md` section 2.5."""

    start = KEYWORD_SECTION.search(markdown)
    if start is None:
        raise ValueError("GRAMMAR.md does not contain a `### 2.5 Keywords` section")
    rest = markdown[start.end() :]
    end = NEXT_SECTION.search(rest)
    return rest[: end.start()] if end else rest


def parse_specification_keywords(markdown: str) -> SpecificationKeywords:
    """Extract the reserved, numeric-constant, and contextual sets from section 2.5."""

    section = keyword_section(markdown)

    fenced = FENCED_BLOCK.search(section)
    if fenced is None:
        raise ValueError("GRAMMAR.md section 2.5 has no fenced reserved-word block")
    reserved = frozenset(fenced.group(1).split())

    paragraph = NUMERIC_CONSTANT_PARAGRAPH.search(section)
    if paragraph is None:
        raise ValueError("GRAMMAR.md section 2.5 has no `Built-in numeric constants` paragraph")
    numeric_constants = frozenset(
        name
        for name in BACKTICKED.findall(paragraph.group(0))
        # The same paragraph names the token types (`CONST_INF`); only the lexemes count.
        if name.isupper() and not name.startswith("CONST_")
    )

    heading = CONTEXTUAL_HEADING.search(section)
    if heading is None:
        raise ValueError("GRAMMAR.md section 2.5 has no `**Contextual keywords**` list")
    contextual: set[str] = set()
    for bullet in BULLET.findall(section[heading.end() :]):
        # Everything after the em dash is prose that may mention unrelated keywords.
        contextual.update(BACKTICKED.findall(bullet.split("—")[0]))

    if not reserved or not numeric_constants or not contextual:
        raise ValueError("GRAMMAR.md section 2.5 parsed to an empty keyword set")

    return SpecificationKeywords(reserved, numeric_constants, frozenset(contextual))


def alternation_words(pattern: str) -> frozenset[str]:
    """Return the lexemes of a generated `\\b(?:a|b)\\b` keyword alternation."""

    match = ALTERNATION.match(pattern)
    if match is None:
        raise ValueError(f"generated keyword rule is not a plain alternation: {pattern!r}")
    return frozenset(match.group(1).split("|"))


def grammar_keyword_words(grammar: dict[str, Any]) -> GrammarWords:
    """Return the three word sets the generated grammar highlights."""

    repository = grammar["repository"]
    reserved: set[str] = set()
    for rule in repository["keywords"]["patterns"]:
        reserved.update(alternation_words(rule["match"]))

    constants = repository["constants"]["patterns"]
    if len(constants) != 2:
        raise ValueError("the generated `constants` entry no longer holds exactly two rules")
    return GrammarWords(
        frozenset(reserved),
        alternation_words(constants[0]["match"]),
        alternation_words(constants[1]["match"]),
    )


def _difference(label: str, left: frozenset[str], left_name: str, right: frozenset[str], right_name: str) -> list[str]:
    violations = []
    only_left = sorted(left - right)
    if only_left:
        violations.append(f"{label}: {left_name} has {only_left} but {right_name} does not")
    only_right = sorted(right - left)
    if only_right:
        violations.append(f"{label}: {right_name} has {only_right} but {left_name} does not")
    return violations


def find_violations(
    tokenizer_reserved: frozenset[str],
    tokenizer_numeric_constants: frozenset[str],
    specification: SpecificationKeywords,
    grammar: GrammarWords,
) -> list[str]:
    """Return every disagreement between the three keyword descriptions."""

    violations: list[str] = []
    violations += _difference(
        "reserved words", tokenizer_reserved, "the tokenizer table", specification.reserved, "GRAMMAR.md section 2.5"
    )
    violations += _difference(
        "numeric keyword constants",
        tokenizer_numeric_constants,
        "the tokenizer table",
        specification.numeric_constants,
        "GRAMMAR.md section 2.5",
    )
    violations += _difference(
        "reserved words",
        tokenizer_reserved,
        "the tokenizer table",
        grammar.reserved,
        "the generated TextMate grammar",
    )
    violations += _difference(
        "numeric keyword constants",
        tokenizer_numeric_constants,
        "the tokenizer table",
        grammar.numeric_constants,
        "the generated TextMate grammar",
    )

    promoted = sorted(specification.contextual & (tokenizer_reserved | tokenizer_numeric_constants))
    if promoted:
        violations.append(
            f"contextual keywords: GRAMMAR.md section 2.5 documents {promoted} as ordinary "
            "identifiers, but the tokenizer reserves them"
        )

    globally_reserved = sorted(specification.contextual & (grammar.reserved | grammar.numeric_constants))
    if globally_reserved:
        violations.append(
            f"contextual keywords: the generated TextMate grammar globally reserves {globally_reserved}, "
            "which section 2.5 documents as ordinary identifiers"
        )

    reserved_literals = sorted(grammar.literal_constants & (tokenizer_reserved | tokenizer_numeric_constants))
    if reserved_literals:
        violations.append(
            f"literal constants: the generated TextMate grammar scopes {reserved_literals} as literals, "
            "but the tokenizer now reserves them"
        )

    return violations


def run(
    tokenizer: Path,
    specification_path: Path,
    patterns_directory: Path,
    builder_path: Path,
) -> list[str]:
    """Generate the grammar from the given inputs and return every violation."""

    builder = load_builder(builder_path)
    tokenizer_source = tokenizer.read_text(encoding="utf-8")
    keywords = builder.parse_tokenizer_keywords(tokenizer_source)
    grammar = builder.build_grammar(tokenizer_source, builder.load_pattern_inputs(patterns_directory))
    return find_violations(
        frozenset(keywords.reserved),
        frozenset(keywords.numeric_constants),
        parse_specification_keywords(specification_path.read_text(encoding="utf-8")),
        grammar_keyword_words(grammar),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tokenizer", type=Path, default=DEFAULT_TOKENIZER)
    parser.add_argument("--specification", type=Path, default=DEFAULT_SPECIFICATION)
    parser.add_argument("--patterns-dir", type=Path, default=DEFAULT_PATTERNS_DIRECTORY)
    parser.add_argument("--builder", type=Path, default=DEFAULT_BUILDER)
    arguments = parser.parse_args(argv)

    try:
        violations = run(arguments.tokenizer, arguments.specification, arguments.patterns_dir, arguments.builder)
    except Exception as error:  # A malformed input is itself a drift report.
        print(f"FoundryScript TextMate grammar check failed: {error}", file=sys.stderr)
        return 1

    for violation in violations:
        print(violation, file=sys.stderr)
    if violations:
        print(
            f"{len(violations)} FoundryScript TextMate grammar violation(s); "
            "update GRAMMAR.md section 2.5 and modules/foundry_script/grammar/patterns/keyword_scopes.json",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
