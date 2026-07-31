# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_foundryscript_tmlanguage.py.

Each keyword-drift class is injected into a copy of the real inputs and the checker is
then executed -- as a function and as a process -- so the violations under test are the
ones the check would actually report, never a substring of its source.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_foundryscript_tmlanguage as check  # noqa: E402

CHECKER = REPO_ROOT / "misc/checks/check_foundryscript_tmlanguage.py"
TOKENIZER = REPO_ROOT / "modules/foundry_script/fs_tokenizer.cpp"
SPECIFICATION = REPO_ROOT / "modules/foundry_script/GRAMMAR.md"
PATTERNS = REPO_ROOT / "modules/foundry_script/grammar/patterns"


class SpecificationParsingTests(unittest.TestCase):
    """Section 2.5 has to yield three sets, not one flat keyword list."""

    keywords: Any

    @classmethod
    def setUpClass(cls) -> None:
        cls.keywords = check.parse_specification_keywords(SPECIFICATION.read_text(encoding="utf-8"))

    def test_the_fenced_block_supplies_only_reserved_words(self) -> None:
        self.assertIn("class_name", self.keywords.reserved)
        self.assertIn("yield", self.keywords.reserved)
        # The fenced block deliberately omits the numeric keyword constants.
        self.assertEqual(frozenset(), self.keywords.reserved & self.keywords.numeric_constants)

    def test_the_numeric_constants_are_parsed_from_their_own_paragraph(self) -> None:
        self.assertEqual({"INF", "NAN", "PI", "TAU"}, set(self.keywords.numeric_constants))

    def test_contextual_keywords_are_a_third_disjoint_set(self) -> None:
        self.assertIn("annotation", self.keywords.contextual)
        self.assertIn("extend", self.keywords.contextual)
        self.assertIn("targets", self.keywords.contextual)
        self.assertIn("get", self.keywords.contextual)
        self.assertIn("set", self.keywords.contextual)
        self.assertEqual(frozenset(), self.keywords.contextual & self.keywords.reserved)

    def test_a_section_without_a_fenced_block_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            check.parse_specification_keywords("### 2.5 Keywords\n\nNo block here.\n\n### 2.6 Literals\n")

    def test_a_missing_section_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            check.parse_specification_keywords("# Grammar\n")


class AlternationParsingTests(unittest.TestCase):
    def test_a_generated_alternation_yields_its_lexemes(self) -> None:
        self.assertEqual({"class_name", "class"}, set(check.alternation_words(r"\b(?:class_name|class)\b")))

    def test_a_hand_written_rule_shape_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            check.alternation_words(r"\bclass\b")


class ViolationTests(unittest.TestCase):
    """The pure comparison, driven with synthetic sets."""

    tokenizer_reserved = frozenset({"func", "var"})
    tokenizer_constants = frozenset({"PI"})

    def specification(
        self,
        reserved: set[str] | None = None,
        constants: set[str] | None = None,
        contextual: set[str] | None = None,
    ) -> Any:
        return check.SpecificationKeywords(
            frozenset(reserved if reserved is not None else self.tokenizer_reserved),
            frozenset(constants if constants is not None else self.tokenizer_constants),
            frozenset(contextual if contextual is not None else {"async"}),
        )

    def grammar(
        self,
        reserved: set[str] | None = None,
        constants: set[str] | None = None,
        literals: set[str] | None = None,
    ) -> Any:
        return check.GrammarWords(
            frozenset(reserved if reserved is not None else self.tokenizer_reserved),
            frozenset(constants if constants is not None else self.tokenizer_constants),
            frozenset(literals if literals is not None else {"true", "false", "null"}),
        )

    def violations(self, **overrides: Any) -> list[str]:
        found: list[str] = check.find_violations(
            self.tokenizer_reserved,
            self.tokenizer_constants,
            overrides.get("specification") or self.specification(),
            overrides.get("grammar") or self.grammar(),
        )
        return found

    def test_aligned_inputs_report_nothing(self) -> None:
        self.assertEqual([], self.violations())

    def test_a_keyword_missing_from_the_specification_is_reported(self) -> None:
        violations = self.violations(specification=self.specification(reserved={"func"}))
        self.assertEqual(1, len(violations))
        self.assertIn("'var'", violations[0])

    def test_a_numeric_constant_missing_from_the_specification_is_reported(self) -> None:
        violations = self.violations(specification=self.specification(constants=set()))
        self.assertEqual(1, len(violations))
        self.assertIn("numeric keyword constants", violations[0])

    def test_a_keyword_missing_from_the_generated_grammar_is_reported(self) -> None:
        violations = self.violations(grammar=self.grammar(reserved={"func"}))
        self.assertEqual(1, len(violations))
        self.assertIn("the generated TextMate grammar", violations[0])

    def test_a_numeric_constant_folded_into_the_reserved_rule_is_reported(self) -> None:
        # Exactly the failure mode of treating section 2.5 as one flat keyword list.
        violations = self.violations(grammar=self.grammar(reserved={"func", "var", "PI"}, constants=set()))
        self.assertEqual(2, len(violations))

    def test_a_contextual_keyword_the_tokenizer_reserved_is_reported(self) -> None:
        violations = self.violations(specification=self.specification(contextual={"func"}))
        self.assertTrue(any("ordinary identifiers, but the tokenizer reserves them" in entry for entry in violations))

    def test_a_contextual_keyword_reserved_by_the_grammar_is_reported(self) -> None:
        violations = self.violations(
            specification=self.specification(contextual={"async"}),
            grammar=self.grammar(reserved={"func", "var", "async"}),
        )
        self.assertTrue(any("globally reserves ['async']" in entry for entry in violations))

    def test_a_literal_constant_that_became_a_keyword_is_reported(self) -> None:
        violations = self.violations(grammar=self.grammar(literals={"true", "func"}))
        self.assertTrue(any("literal constants" in entry for entry in violations))

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        violations = self.violations(
            specification=self.specification(reserved=set(), constants=set()),
            grammar=self.grammar(reserved=set(), constants=set()),
        )
        self.assertEqual(4, len(violations))


class CheckerExecutionTests(unittest.TestCase):
    """Run the checker itself, against the real tree and against injected drift."""

    def run_checker(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), *arguments],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )

    def test_the_repository_is_currently_free_of_keyword_drift(self) -> None:
        result = self.run_checker()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("", result.stderr)

    def run_with_new_keyword(self, directory: str, scope: str | None) -> subprocess.CompletedProcess[str]:
        """Add `wherever` to a copy of the tokenizer table, optionally giving it a scope."""

        tokenizer = Path(directory) / "fs_tokenizer.cpp"
        tokenizer.write_text(
            TOKENIZER.read_text(encoding="utf-8").replace(
                'KEYWORD("while", Token::WHILE)',
                'KEYWORD("while", Token::WHILE) KEYWORD("wherever", Token::WHEREVER)',
            ),
            encoding="utf-8",
        )
        if scope is None:
            return self.run_checker("--tokenizer", str(tokenizer))

        patterns = Path(directory) / "patterns"
        shutil.copytree(PATTERNS, patterns)
        scopes_path = patterns / "keyword_scopes.json"
        scopes = json.loads(scopes_path.read_text(encoding="utf-8"))
        scopes["reserved"]["wherever"] = scope
        scopes_path.write_text(json.dumps(scopes, indent=2) + "\n", encoding="utf-8")
        return self.run_checker("--tokenizer", str(tokenizer), "--patterns-dir", str(patterns))

    def test_a_new_keyword_without_a_scope_fails_before_comparison(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_with_new_keyword(directory, scope=None)
        self.assertEqual(1, result.returncode)
        self.assertIn("'wherever'", result.stderr)
        self.assertIn("keyword_scopes.json", result.stderr)

    def test_a_scoped_keyword_absent_from_the_specification_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_with_new_keyword(directory, scope="keyword.control.foundryscript")
        self.assertEqual(1, result.returncode)
        self.assertIn("'wherever'", result.stderr)
        self.assertIn("GRAMMAR.md section 2.5", result.stderr)

    def test_a_reserved_word_without_a_scope_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            patterns = Path(directory) / "patterns"
            shutil.copytree(PATTERNS, patterns)
            scopes_path = patterns / "keyword_scopes.json"
            scopes = json.loads(scopes_path.read_text(encoding="utf-8"))
            del scopes["reserved"]["while"]
            scopes_path.write_text(json.dumps(scopes, indent=2) + "\n", encoding="utf-8")
            result = self.run_checker("--patterns-dir", str(patterns))
        self.assertEqual(1, result.returncode)
        self.assertIn("while", result.stderr)

    def test_a_contextual_keyword_scoped_as_reserved_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            patterns = Path(directory) / "patterns"
            shutil.copytree(PATTERNS, patterns)
            scopes_path = patterns / "keyword_scopes.json"
            scopes = json.loads(scopes_path.read_text(encoding="utf-8"))
            scopes["reserved"]["async"] = "storage.modifier.foundryscript"
            scopes_path.write_text(json.dumps(scopes, indent=2) + "\n", encoding="utf-8")
            result = self.run_checker("--patterns-dir", str(patterns))
        self.assertEqual(1, result.returncode)
        self.assertIn("async", result.stderr)

    def test_a_missing_specification_is_reported_rather_than_crashing(self) -> None:
        result = self.run_checker("--specification", str(REPO_ROOT / "does-not-exist.md"))
        self.assertEqual(1, result.returncode)
        self.assertIn("FoundryScript TextMate grammar check failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
