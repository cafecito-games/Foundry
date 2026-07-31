# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Behavioral tests for the generated FoundryScript TextMate grammar.

The grammar is generated, never committed, so these tests run the generator and then run
the produced document through a real Oniguruma-backed TextMate interpreter, asserting on
the scopes an editor would actually apply to a representative `.fs` source file.
"""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
GRAMMAR_DIRECTORY = REPO_ROOT / "modules/foundry_script/grammar"
BUILDER = GRAMMAR_DIRECTORY / "tmlanguage_builder.py"
SAMPLE = GRAMMAR_DIRECTORY / "fixtures/highlighting_sample.fs"

sys.path.insert(0, str(REPO_ROOT / "tests/python_build"))

try:
    import onigurumacffi  # noqa: F401
    from textmate_tokenizer import TextMateGrammar

    ONIGURUMA_AVAILABLE = True
except ImportError:  # pragma: no cover - exercised only on hosts without the binding
    ONIGURUMA_AVAILABLE = False


def load_builder() -> Any:
    specification = importlib.util.spec_from_file_location("tmlanguage_builder", BUILDER)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


builder = load_builder()


class GenerationTests(unittest.TestCase):
    """The artifact the release workflow publishes has to be reproducible."""

    def test_repeated_generation_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.json"
            second = Path(directory) / "second.json"
            for output in (first, second):
                result = subprocess.run(
                    [sys.executable, str(BUILDER), "--output", str(output)],
                    capture_output=True,
                    text=True,
                    cwd=REPO_ROOT,
                )
                self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_the_generator_writes_the_same_document_it_prints(self) -> None:
        result = subprocess.run(
            [sys.executable, str(BUILDER)],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(builder.generate(), result.stdout)

    def test_the_document_declares_the_published_scope_and_file_type(self) -> None:
        grammar = json.loads(builder.generate())
        self.assertEqual("source.foundryscript", grammar["scopeName"])
        self.assertEqual(["fs"], grammar["fileTypes"])

    def test_every_include_resolves_to_a_repository_entry(self) -> None:
        grammar = json.loads(builder.generate())
        repository = grammar["repository"]

        def walk(rules: list[dict[str, Any]]) -> None:
            for rule in rules:
                include = rule.get("include")
                if include is not None and include != "$self":
                    self.assertIn(include.removeprefix("#"), repository)
                walk(rule.get("patterns") or [])

        walk(grammar["patterns"])
        for entry in repository.values():
            walk(entry.get("patterns") or [])

    def test_a_reserved_word_without_a_scope_is_a_generation_error(self) -> None:
        inputs = builder.load_pattern_inputs(GRAMMAR_DIRECTORY / "patterns")
        inputs["keyword_scopes.json"]["reserved"].pop("func")
        with self.assertRaises(builder.GrammarBuildError):
            builder.build_grammar((REPO_ROOT / "modules/foundry_script/fs_tokenizer.cpp").read_text(), inputs)


@unittest.skipUnless(ONIGURUMA_AVAILABLE, "onigurumacffi is required to evaluate TextMate patterns")
class TokenizationTests(unittest.TestCase):
    """Scope assertions driven through the real Oniguruma engine."""

    grammar: Any
    sample: str
    sample_scopes: list[tuple[str, ...]]

    @classmethod
    def setUpClass(cls) -> None:
        cls.grammar = TextMateGrammar(json.loads(builder.generate()))
        cls.sample = SAMPLE.read_text(encoding="utf-8")
        cls.sample_scopes = cls.grammar.scope_map(cls.sample)

    def scopes_at(self, marker: str, offset: int = 0, source: str | None = None) -> tuple[str, ...]:
        """Return the scope stack applied to `marker`'s `offset`-th character."""

        text = self.sample if source is None else source
        scopes: list[tuple[str, ...]] = self.sample_scopes if source is None else self.grammar.scope_map(source)
        return scopes[text.index(marker) + offset]

    def assertScoped(self, marker: str, scope: str, offset: int = 0, source: str | None = None) -> None:
        self.assertIn(scope, self.scopes_at(marker, offset, source), f"{marker!r} was not scoped {scope!r}")

    def assertNotScoped(self, marker: str, scope: str, offset: int = 0, source: str | None = None) -> None:
        self.assertNotIn(scope, self.scopes_at(marker, offset, source), f"{marker!r} was scoped {scope!r}")

    # -- comments ---------------------------------------------------------

    def test_documentation_comments_win_over_ordinary_comments(self) -> None:
        self.assertScoped("## Documentation", "comment.line.number-sign.documentation.foundryscript")
        self.assertScoped("# Ordinary", "comment.line.number-sign.foundryscript")
        self.assertNotScoped("# Ordinary", "comment.line.number-sign.documentation.foundryscript")

    def test_a_hash_inside_a_string_is_not_a_comment(self) -> None:
        self.assertScoped("# that is not a comment", "string.quoted.triple.double.foundryscript")
        self.assertNotScoped("# that is not a comment", "comment.line.number-sign.foundryscript")

    # -- strings ----------------------------------------------------------

    def test_triple_quoted_strings_win_over_short_strings(self) -> None:
        self.assertScoped('"""\nTriple', "string.quoted.triple.double.foundryscript")
        self.assertScoped("'''single triple'''", "string.quoted.triple.single.foundryscript")
        self.assertNotScoped("'''single triple'''", "string.quoted.single.foundryscript")

    def test_short_strings_are_scoped_as_short_strings(self) -> None:
        self.assertScoped('"escaped', "string.quoted.double.foundryscript")
        self.assertNotScoped('"escaped', "string.quoted.triple.double.foundryscript")

    def test_string_prefixes_are_scoped_separately_from_the_body(self) -> None:
        for marker in ('r"C:', '&"unique_name"', '^"res://scene.tscn"'):
            with self.subTest(prefix=marker[0]):
                self.assertScoped(marker, "storage.type.string.foundryscript")
                self.assertScoped(marker, "punctuation.definition.string.begin.foundryscript", offset=1)

    def test_escape_sequences_inside_a_string_are_scoped(self) -> None:
        self.assertScoped("\\n \\u00e9", "constant.character.escape.foundryscript")
        self.assertScoped("\\u00e9", "constant.character.escape.foundryscript")

    def test_a_backslash_before_the_newline_continues_the_string(self) -> None:
        self.assertScoped("\\\nsecond part", "constant.character.escape.foundryscript")
        self.assertScoped("second part", "string.quoted.double.foundryscript")
        self.assertNotScoped("\\\nsecond part", "invalid.illegal.unclosed-string.foundryscript", offset=1)
        # The string closes on its own line, so the next declaration is code again.
        self.assertScoped("var annotation", "storage.type.var.foundryscript")

    def test_a_raw_string_keeps_ordinary_backslashes_literal(self) -> None:
        self.assertScoped("C:\\path", "string.quoted.double.raw.foundryscript", offset=2)
        # `\\n` inside `\\no` is an ordinary character pair in a raw string.
        self.assertNotScoped("\\no\\escapes", "constant.character.escape.foundryscript")
        # The two sequences a raw string still honors, so it can hold a quote.
        self.assertScoped('a \\" b', "constant.character.escape.foundryscript", offset=2)
        self.assertScoped("b \\\\ c", "constant.character.escape.foundryscript", offset=2)

    # -- numbers ----------------------------------------------------------

    def test_numeric_literal_bases_are_distinguished(self) -> None:
        self.assertScoped("0xFF_00", "constant.numeric.hex.foundryscript")
        self.assertScoped("0b1010_1010", "constant.numeric.binary.foundryscript")
        self.assertScoped("1.5e-3", "constant.numeric.float.foundryscript")
        self.assertScoped("100\n", "constant.numeric.integer.foundryscript")

    def test_a_range_operand_is_a_number_and_the_range_token_is_punctuation(self) -> None:
        self.assertScoped("1..2", "constant.numeric.integer.foundryscript")
        self.assertScoped("1..2", "punctuation.separator.rest.foundryscript", offset=1)
        self.assertScoped("1..2", "constant.numeric.integer.foundryscript", offset=3)

    def test_a_tuple_index_is_not_a_number(self) -> None:
        self.assertNotScoped("pair.0", "constant.numeric.integer.foundryscript", offset=5)
        self.assertNotScoped("pair.0", "constant.numeric.float.foundryscript", offset=4)

    # -- keywords and constants -------------------------------------------

    def test_reserved_words_carry_their_generated_scope(self) -> None:
        self.assertScoped("func remainder", "storage.type.function.foundryscript")
        self.assertScoped("await node.ready", "keyword.control.flow.await.foundryscript")
        self.assertScoped("match path:", "keyword.control.match.foundryscript")

    def test_numeric_keyword_constants_are_language_constants(self) -> None:
        for marker in ("TAU", "PI", "INF", "NAN"):
            with self.subTest(constant=marker):
                self.assertScoped(marker, "constant.language.numeric.foundryscript")

    def test_boolean_and_null_literals_are_language_constants(self) -> None:
        source = "var flags := [true, false, null]\n"
        for marker in ("true", "false", "null"):
            with self.subTest(literal=marker):
                self.assertScoped(marker, "constant.language.foundryscript", source=source)

    def test_yield_is_marked_illegal(self) -> None:
        self.assertScoped("yield", "invalid.illegal.yield.foundryscript", source="yield value\n")

    # -- annotations ------------------------------------------------------

    def test_annotations_scope_their_qualified_name(self) -> None:
        self.assertScoped("@tool", "punctuation.definition.annotation.foundryscript")
        self.assertScoped("@tool", "entity.name.function.annotation.foundryscript", offset=1)
        self.assertScoped("@cafecito.test.timeout", "entity.name.function.annotation.foundryscript", offset=1)
        # The whole dotted name belongs to the annotation, not just its first segment.
        self.assertScoped("@cafecito.test.timeout", "entity.name.function.annotation.foundryscript", offset=21)

    # -- node references --------------------------------------------------

    def test_node_paths_are_scoped_including_unique_name_segments(self) -> None:
        self.assertScoped("$Player/%Weapon/Barrel", "punctuation.definition.node.foundryscript")
        self.assertScoped("$Player/%Weapon/Barrel", "variable.other.node.foundryscript", offset=1)
        self.assertScoped("$Player/%Weapon/Barrel", "variable.other.node.foundryscript", offset=9)

    def test_a_quoted_node_path_is_scoped_as_a_node_reference(self) -> None:
        self.assertScoped('$"Player Two"', "punctuation.definition.node.foundryscript")
        self.assertScoped('$"Player Two"', "string.quoted.node.foundryscript", offset=1)

    def test_a_quoted_segment_inside_a_node_path_stays_a_node_reference(self) -> None:
        marker = '$Player/"Weapon Slot"/%Barrel'
        self.assertScoped(marker, "punctuation.definition.node.foundryscript")
        self.assertScoped(marker, "string.quoted.node.foundryscript", offset=8)
        self.assertScoped(marker, "punctuation.definition.node.unique.foundryscript", offset=22)
        self.assertScoped(marker, "variable.other.node.foundryscript", offset=23)
        # The path separators must not be read as division around a string.
        self.assertNotScoped(marker, "keyword.operator.arithmetic.foundryscript", offset=7)

    def test_a_leading_percent_is_a_unique_name_reference(self) -> None:
        self.assertScoped(":= %Weapon", "punctuation.definition.node.foundryscript", offset=3)
        self.assertScoped(":= %Weapon", "variable.other.node.foundryscript", offset=4)

    def test_a_percent_between_values_is_the_modulo_operator(self) -> None:
        self.assertScoped("left % right", "keyword.operator.arithmetic.foundryscript", offset=5)
        self.assertNotScoped("left % right", "punctuation.definition.node.foundryscript", offset=5)

    def test_one_sided_spacing_does_not_turn_modulo_into_a_node_reference(self) -> None:
        self.assertScoped("left %right", "keyword.operator.arithmetic.foundryscript", offset=5)
        self.assertNotScoped("left %right", "variable.other.node.foundryscript", offset=6)

    def test_a_percent_after_a_keyword_is_still_a_node_reference(self) -> None:
        self.assertScoped("not %Weapon", "punctuation.definition.node.foundryscript", offset=4)
        self.assertScoped("not %Weapon", "variable.other.node.foundryscript", offset=5)

    def test_modulo_without_spaces_is_still_an_operator(self) -> None:
        source = "var remainder := left%right\n"
        self.assertScoped("left%right", "keyword.operator.arithmetic.foundryscript", offset=4, source=source)
        self.assertNotScoped("left%right", "variable.other.node.foundryscript", offset=5, source=source)

    # -- contextual keywords ----------------------------------------------

    def test_contextual_keywords_are_scoped_only_in_their_construct(self) -> None:
        self.assertScoped("annotation Timeout", "storage.type.annotation.foundryscript")
        self.assertScoped("extend Sample.Support.Helper", "storage.modifier.extend.foundryscript")
        self.assertScoped("async func fetch", "storage.modifier.async.foundryscript")
        self.assertScoped(") targets METHOD", "keyword.other.targets.foundryscript", offset=2)
        self.assertScoped("get:", "storage.type.accessor.foundryscript")
        self.assertScoped("set(value)", "storage.type.accessor.foundryscript")

    def test_contextual_keywords_used_as_identifiers_stay_identifiers(self) -> None:
        cases = {
            "var annotation": "storage.type.annotation.foundryscript",
            "var extend": "storage.modifier.extend.foundryscript",
            "var async": "storage.modifier.async.foundryscript",
            "var targets": "keyword.other.targets.foundryscript",
            "var get": "storage.type.accessor.foundryscript",
        }
        for marker, forbidden in cases.items():
            with self.subTest(identifier=marker):
                self.assertNotScoped(marker, forbidden, offset=4)
                self.assertScoped(marker, "variable.other.declaration.foundryscript", offset=4)

    def test_a_statement_level_get_or_set_call_is_not_an_accessor(self) -> None:
        for marker in ('get("health")', 'set("health", 1)'):
            with self.subTest(call=marker):
                self.assertNotScoped(marker, "storage.type.accessor.foundryscript")
                self.assertScoped(marker, "entity.name.function.call.foundryscript")

    # -- declarations and types -------------------------------------------

    def test_declaration_names_are_scoped_without_restating_keyword_scopes(self) -> None:
        self.assertScoped("GrammarSample", "entity.name.type.foundryscript")
        self.assertScoped("remainder", "entity.name.function.foundryscript")
        self.assertScoped("damaged", "entity.name.function.signal.foundryscript")
        self.assertScoped("health: int", "variable.other.declaration.foundryscript")

    def test_a_type_position_scopes_a_capitalized_name(self) -> None:
        self.assertScoped("-> Int", "entity.name.type.foundryscript", offset=3, source="func f() -> Int:\n\tpass\n")
        self.assertScoped(": String", "entity.name.type.foundryscript", offset=2)

    def test_a_call_is_not_confused_with_a_control_keyword(self) -> None:
        source = "func f():\n\tif (a):\n\t\tprint(a)\n"
        self.assertScoped("if (a)", "keyword.control.conditional.foundryscript", source=source)
        self.assertNotScoped("if (a)", "entity.name.function.call.foundryscript", source=source)
        self.assertScoped("print(a)", "entity.name.function.call.foundryscript", source=source)


if __name__ == "__main__":
    unittest.main()
