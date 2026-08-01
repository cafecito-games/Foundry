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

    # -- lowercase built-in types in type positions ------------------------

    def test_lowercase_built_in_types_are_scoped_in_syntactic_type_positions(self) -> None:
        source = (
            "var count: int = 0\n"
            "\n"
            "func convert(value: float) -> bool:\n"
            "\treturn value != 0.0\n"
            "\n"
            "var values: Array[int] = []\n"
        )
        self.assertScoped(": int", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped(": float", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped("-> bool", "entity.name.type.foundryscript", offset=3, source=source)
        self.assertScoped("Array[int]", "entity.name.type.foundryscript", offset=0, source=source)
        self.assertScoped("Array[int]", "entity.name.type.foundryscript", offset=6, source=source)

    def test_lowercase_built_in_types_stay_contextual_outside_type_positions(self) -> None:
        source = "var int = 0\nint += 1\nprint(int)\n"
        self.assertNotScoped("var int", "entity.name.type.foundryscript", offset=4, source=source)
        self.assertScoped("var int", "variable.other.declaration.foundryscript", offset=4, source=source)
        self.assertNotScoped("int += 1", "entity.name.type.foundryscript", source=source)
        self.assertNotScoped("print(int)", "entity.name.type.foundryscript", offset=6, source=source)
        self.assertScoped("print(int)", "entity.name.function.call.foundryscript", source=source)

    def test_a_bracket_only_opens_a_type_argument_position_after_a_type_name(self) -> None:
        # `[` also opens an array literal; only an identifier immediately before `[`
        # (a generic type argument list, e.g. `Array[int]`) is a type position.
        source = "var literal = [int, float, bool]\n"
        for marker, offset in (("int", 0), ("float", 0), ("bool", 0)):
            with self.subTest(word=marker):
                self.assertNotScoped(marker, "entity.name.type.foundryscript", offset=offset, source=source)

    def test_a_lowercase_subscript_target_is_not_a_type_argument_list(self) -> None:
        # `values[int]` is a subscript of the lowercase variable `values`, not a
        # generic type-argument list; only a capitalized (type-shaped) identifier
        # immediately before `[` introduces a type-argument position.
        source = "var picked = values[int]\n"
        self.assertNotScoped("[int]", "entity.name.type.foundryscript", offset=1, source=source)

    def test_capitalized_type_coverage_still_works(self) -> None:
        self.assertScoped("-> Int", "entity.name.type.foundryscript", offset=3, source="func f() -> Int:\n\tpass\n")
        self.assertScoped(": String", "entity.name.type.foundryscript", offset=2)

    # -- annotation declaration `targets` -----------------------------------

    def test_targets_is_scoped_in_both_annotation_declaration_forms(self) -> None:
        parameterized = "annotation timed(seconds: float) targets CLASS, METHOD:\n\tpass\n"
        parameterless = "annotation marker targets CLASS, METHOD:\n\tpass\n"
        self.assertScoped("targets", "keyword.other.targets.foundryscript", source=parameterized)
        self.assertScoped("targets", "keyword.other.targets.foundryscript", source=parameterless)

    def test_targets_is_not_scoped_outside_an_annotation_declaration(self) -> None:
        source = "var targets = 1\ntargets()\n"
        self.assertNotScoped("var targets", "keyword.other.targets.foundryscript", offset=4, source=source)
        self.assertScoped("var targets", "variable.other.declaration.foundryscript", offset=4, source=source)
        self.assertNotScoped("targets()", "keyword.other.targets.foundryscript", source=source)
        self.assertScoped("targets()", "entity.name.function.call.foundryscript", source=source)

    def test_targets_is_scoped_when_the_parameter_list_spans_multiple_lines(self) -> None:
        source = "annotation multiline(\n\tseconds: float\n) targets METHOD, CLASS:\n\tpass\n"
        self.assertScoped(") targets", "keyword.other.targets.foundryscript", offset=2, source=source)
        # The parameter list is still tokenized while the block is open.
        self.assertScoped("float", "entity.name.type.foundryscript", source=source)

    def test_a_typed_parameter_named_targets_does_not_end_the_header_early(self) -> None:
        source = "annotation marker(targets: int) targets METHOD:\n\tpass\n"
        # The real separator is the second `targets`, after the closing paren.
        self.assertScoped(") targets", "keyword.other.targets.foundryscript", offset=2, source=source)

    def test_a_default_value_named_targets_does_not_end_the_header_early(self) -> None:
        source = "annotation marker(kind: String = targets) targets METHOD:\n\tpass\n"
        # The real separator is the second `targets`, after the closing paren.
        self.assertScoped(") targets", "keyword.other.targets.foundryscript", offset=2, source=source)

    def test_a_declaration_without_targets_does_not_swallow_the_rest_of_the_file(self) -> None:
        # Malformed/mid-edit input: the annotation body never reaches `targets`. The
        # block must not leak its scope past the next root declaration.
        source = "annotation broken(\n\nfunc after() -> int:\n\treturn 0\n"
        self.assertScoped("func after", "storage.type.function.foundryscript", source=source)
        self.assertScoped("after", "entity.name.function.foundryscript", source=source)

    # -- property accessors -------------------------------------------------

    def test_every_accessor_form_is_scoped_as_an_accessor(self) -> None:
        source = (
            "var health: int:\n"
            "\tget:\n"
            "\t\treturn field\n"
            "\n"
            "var armor: int:\n"
            "\tget():\n"
            "\t\treturn field\n"
            "\n"
            "var speed: int:\n"
            "\tget = read_speed\n"
            "\tset = write_speed\n"
            "\n"
            "var mana: int:\n"
            "\tset(value):\n"
            "\t\tfield = value\n"
        )
        self.assertScoped("get:", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("get():", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("get = read_speed", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("set = write_speed", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("set(value):", "storage.type.accessor.foundryscript", source=source)

    def test_get_and_set_declarations_are_unaffected(self) -> None:
        source = "func get(key: String) -> int:\n\treturn 0\n\nfunc set(key: String, value: int) -> void:\n\tpass\n"
        self.assertScoped("get", "entity.name.function.foundryscript", source=source)
        self.assertNotScoped("get", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("set", "entity.name.function.foundryscript", source=source)
        self.assertNotScoped("set", "storage.type.accessor.foundryscript", source=source)

    def test_a_multi_argument_or_non_identifier_call_is_never_an_accessor(self) -> None:
        # `get`'s inline form only ever has empty parens and `set`'s only ever takes a
        # bare identifier (GRAMMAR.md 4.4); a call shaped any other way -- including one
        # that happens to be followed by `:` because it is a dict-literal key -- is left
        # to the ordinary call rule instead of being misread as an accessor.
        source = 'get("health"): 1\nset("health", 1): 2\n'
        for marker in ('get("health"):', 'set("health", 1):'):
            with self.subTest(call=marker):
                self.assertNotScoped(marker, "storage.type.accessor.foundryscript", source=source)

    # -- triple-quoted node-path components ---------------------------------

    def test_triple_quoted_node_path_components_are_consumed_completely(self) -> None:
        source = (
            'var first = $"""Some\n'
            'Node"""\n'
            "\n"
            "var second = $'''Some\n"
            "Node'''\n"
            "\n"
            'var third = $Root/"""Some\n'
            'Node"""\n'
            "\n"
            "var fourth = $Root/'''Some\n"
            "Node'''\n"
            "\n"
            "var after = 1\n"
        )
        for marker in ('$"""Some', "$'''Some", '$Root/"""Some', "$Root/'''Some"):
            with self.subTest(marker=marker):
                dollar_offset = marker.index("$")
                self.assertScoped(
                    marker, "punctuation.definition.node.foundryscript", offset=dollar_offset, source=source
                )
                quote_offset = marker.index('"""') if '"""' in marker else marker.index("'''")
                self.assertScoped(marker, "meta.node-path.foundryscript", offset=quote_offset, source=source)
                self.assertScoped(marker, "string.quoted.node.foundryscript", offset=quote_offset, source=source)

        self.assertScoped("var after", "source.foundryscript", source=source)
        self.assertNotScoped("var after", "string.quoted.triple.double.foundryscript", source=source)
        self.assertNotScoped("var after", "string.quoted.triple.single.foundryscript", source=source)
        self.assertNotScoped("var after", "meta.node-path.foundryscript", source=source)

    def test_an_escaped_quote_does_not_end_a_triple_quoted_node_path_early(self) -> None:
        source = 'var escaped = $"""Some \\"""Node"""\nvar after = 1\n'
        self.assertScoped('\\"""Node', "string.quoted.node.foundryscript", offset=0, source=source)
        self.assertScoped('\\"', "constant.character.escape.foundryscript", source=source)
        self.assertScoped("var after", "source.foundryscript", source=source)
        self.assertNotScoped("var after", "meta.node-path.foundryscript", source=source)

    # -- dictionary-literal context ----------------------------------------

    def test_a_single_line_dictionary_value_is_not_scoped_as_a_type(self) -> None:
        source = 'var entry = {"key": int}\n'
        self.assertNotScoped("int}", "entity.name.type.foundryscript", source=source)

    def test_multiline_dictionary_values_are_not_scoped_as_types(self) -> None:
        source = (
            "var values = {\n"
            '\t"builtin": int,\n'
            '\t"native": Node,\n'
            '\t"project": Player,\n'
            '\t"qualified": Game.Player,\n'
            "}\n"
        )
        for marker in ("int,", "Node,", "Player,", "Game.Player,"):
            with self.subTest(value=marker):
                value = marker[:-1]
                self.assertNotScoped(value, "entity.name.type.foundryscript", source=source)

    def test_nested_dictionary_values_are_not_scoped_as_types_at_every_level(self) -> None:
        source = (
            "var nested = {\n"
            '\t"outer": {\n'
            '\t\t"inner": Node,\n'
            '\t\t"deepest": {\n'
            '\t\t\t"leaf": Player,\n'
            "\t\t},\n"
            "\t},\n"
            "}\n"
        )
        self.assertNotScoped("Node,", "entity.name.type.foundryscript", source=source)
        self.assertNotScoped("Player,", "entity.name.type.foundryscript", source=source)

    def test_a_lambda_nested_inside_a_dictionary_value_keeps_its_own_type_scopes(self) -> None:
        source = 'var factories = {\n\t"build": func(value: int) -> Player:\n\t\treturn Player.new(),\n}\n'
        self.assertScoped(": int", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped("-> Player", "entity.name.type.foundryscript", offset=3, source=source)

    def test_a_multiline_lambda_parameter_list_inside_a_dictionary_value_keeps_type_scopes(self) -> None:
        # Each parameter starts its own physical line, which alone looks like a fresh
        # dictionary entry; the parenthesized parameter list must shield them from that.
        source = (
            "var factories = {\n"
            '\t"build": func(\n'
            "\t\tvalue: int,\n"
            "\t\tother: bool,\n"
            "\t) -> Player:\n"
            "\t\treturn Player.new(),\n"
            "}\n"
        )
        self.assertScoped(": int", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped(": bool", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped("-> Player", "entity.name.type.foundryscript", offset=3, source=source)

    def test_a_numeric_dictionary_key_still_hides_a_type_shaped_value(self) -> None:
        source = "var by_index = {\n\t1: Node,\n\t2: int,\n}\n"
        self.assertNotScoped("Node,", "entity.name.type.foundryscript", source=source)
        self.assertNotScoped("int,", "entity.name.type.foundryscript", source=source)

    def test_a_dotted_member_access_dictionary_key_still_hides_a_type_shaped_value(self) -> None:
        source = "var by_owner = {\n\towner.key: Node,\n\towner.key: int,\n}\n"
        self.assertNotScoped("Node,", "entity.name.type.foundryscript", source=source)
        self.assertNotScoped("int,", "entity.name.type.foundryscript", source=source)

    def test_a_reassignment_inside_a_lambda_body_nested_in_a_dictionary_value_is_unaffected(self) -> None:
        # `result = Node.new()` on its own line has the exact same shape as a Lua-style
        # dictionary key, but it is a plain reassignment statement inside the lambda's
        # body, not a fresh dictionary entry; the dictionary context must not swallow it.
        source = 'var factories = {\n\t"build": func() -> Node:\n\t\tresult = Node.new()\n\t\treturn result,\n}\n'
        self.assertScoped("result = Node.new()", "keyword.operator.assignment.foundryscript", offset=7, source=source)
        self.assertScoped("Node.new()", "entity.name.function.call.foundryscript", offset=5, source=source)
        self.assertNotScoped("result = Node.new()", "storage.type.accessor.foundryscript", source=source)

    def test_a_constructor_call_dictionary_value_keeps_call_scope(self) -> None:
        source = "var by_index = {1: Vector2()}\n"
        self.assertScoped("Vector2()", "entity.name.function.call.foundryscript", source=source)
        self.assertNotScoped("Vector2()", "entity.name.type.foundryscript", source=source)

    def test_accessor_shaped_dictionary_keys_are_not_scoped_as_accessors(self) -> None:
        source = (
            "var python_entries = {\n"
            "\tget(): 1,\n"
            "\tget: 2,\n"
            "\tset(value): 3,\n"
            "}\n"
            "\n"
            "var lua_entries = {\n"
            "\tget = read_value,\n"
            "\tset = write_value,\n"
            "}\n"
        )
        for marker in ("get():", "get:", "set(value):", "get = read_value,", "set = write_value,"):
            with self.subTest(key=marker):
                self.assertNotScoped(marker, "storage.type.accessor.foundryscript", source=source)

    def test_callable_dictionary_keys_keep_call_scope(self) -> None:
        source = "var python_entries = {\n\tget(): 1,\n\tset(value): 3,\n}\n"
        self.assertScoped("get():", "entity.name.function.call.foundryscript", source=source)
        self.assertScoped("set(value):", "entity.name.function.call.foundryscript", source=source)

    def test_real_property_accessors_outside_dictionaries_still_scope(self) -> None:
        source = (
            "var health: int:\n"
            "\tget:\n"
            "\t\treturn field\n"
            "\n"
            "var armor: int:\n"
            "\tget():\n"
            "\t\treturn field\n"
            "\n"
            "var speed: int:\n"
            "\tget = read_speed\n"
            "\tset = write_speed\n"
            "\n"
            "var mana: int:\n"
            "\tset(value):\n"
            "\t\tfield = value\n"
        )
        self.assertScoped("get:", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("get():", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("get = read_speed", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("set = write_speed", "storage.type.accessor.foundryscript", source=source)
        self.assertScoped("set(value):", "storage.type.accessor.foundryscript", source=source)

    def test_real_type_positions_outside_dictionaries_still_scope(self) -> None:
        source = (
            "var count: int = 0\n"
            "\n"
            "func convert(value: float) -> bool:\n"
            "\treturn value != 0.0\n"
            "\n"
            "var project: Player = Player.new()\n"
            "var qualified: Game.Player = Game.Player.new()\n"
        )
        self.assertScoped(": int", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped(": float", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped("-> bool", "entity.name.type.foundryscript", offset=3, source=source)
        self.assertScoped(": Player", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped(": Game.Player", "entity.name.type.foundryscript", offset=2, source=source)

    def test_braces_inside_strings_and_comments_do_not_open_or_close_dictionary_context(self) -> None:
        source = 'var text = "not { a dict }"\n# a comment with { braces }\nvar values = {"key": int}\n'
        self.assertNotScoped("key", "entity.name.type.foundryscript", source=source)
        self.assertNotScoped("int}", "entity.name.type.foundryscript", source=source)

    def test_a_declaration_after_a_closed_dictionary_gets_normal_scopes(self) -> None:
        source = (
            'var values = {"key": int}\nvar project: Player = Player.new()\nvar speed: int:\n\tget:\n\t\treturn field\n'
        )
        self.assertScoped("var project", "variable.other.declaration.foundryscript", offset=4, source=source)
        self.assertScoped(": Player", "entity.name.type.foundryscript", offset=2, source=source)
        self.assertScoped("get:", "storage.type.accessor.foundryscript", source=source)

    def test_short_dictionary_value_lowercase_builtins_are_not_types(self) -> None:
        source = 'var values = {"a": int, "b": float, "c": bool}\n'
        for marker in ("int,", "float,", "bool}"):
            with self.subTest(value=marker):
                value = marker[:-1]
                self.assertNotScoped(value, "entity.name.type.foundryscript", source=source)

    def test_short_quoted_node_paths_keep_their_scopes(self) -> None:
        source = (
            'var double_quoted = $"Some Node"\n'
            "var single_quoted = $'Some Node'\n"
            'var mixed = $Root/"Some Node"/%Child\n'
        )
        self.assertScoped('$"Some Node"', "string.quoted.node.foundryscript", offset=1, source=source)
        self.assertScoped("$'Some Node'", "string.quoted.node.foundryscript", offset=1, source=source)
        self.assertNotScoped(
            '$Root/"Some Node"/%Child', "keyword.operator.arithmetic.foundryscript", offset=5, source=source
        )
        self.assertScoped(
            '$Root/"Some Node"/%Child', "punctuation.separator.node.foundryscript", offset=5, source=source
        )


if __name__ == "__main__":
    unittest.main()
