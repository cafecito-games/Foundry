#!/usr/bin/env python3
"""Behavioral coverage for documentation admonition conversion."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

_MODULE_PATH = Path(__file__).resolve().parents[2] / "doc" / "tools" / "make_rst.py"
_spec = importlib.util.spec_from_file_location("make_rst", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
make_rst: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = make_rst
_spec.loader.exec_module(make_rst)


class MakeRstAdmonitionTests(unittest.TestCase):
    @staticmethod
    def context() -> tuple[Any, Any]:
        state = make_rst.State()
        state.current_class = "TestClass"
        return make_rst.ClassDef("TestClass"), state

    def format(self, text: str) -> tuple[str, int]:
        context, state = self.context()
        with contextlib.redirect_stdout(io.StringIO()):
            result = make_rst.format_text_block(text, context, state)
        return result, state.num_errors

    def format_with_diagnostics(self, text: str) -> tuple[str, int, str]:
        context, state = self.context()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = make_rst.format_text_block(text, context, state)
        return result, state.num_errors, output.getvalue()

    @staticmethod
    def error_diagnostic(message: str) -> str:
        return f"{make_rst.Ansi.RED}{make_rst.Ansi.BOLD}ERROR:{make_rst.Ansi.REGULAR} {message}{make_rst.Ansi.RESET}\n"

    def test_converts_each_admonition_type_to_an_indented_directive(self) -> None:
        for admonition in ("note", "warning", "tip", "important"):
            with self.subTest(admonition=admonition):
                result, errors = self.format(f"[{admonition}]Body.[/{admonition}]")
                self.assertEqual(result, f".. classref_{admonition}::\n\n    Body.")
                self.assertEqual(errors, 0)

    def test_preserves_multiline_multiparagraph_content_and_surrounding_prose(self) -> None:
        result, errors = self.format("Before.\n[note]First line.\nSecond line.\n\nSecond paragraph.[/note]\nAfter.")
        self.assertTrue(result.startswith("Before.\n\n.. classref_note::\n\n    First line."))
        self.assertIn("\n    Second line.", result)
        self.assertIn("\n    Second paragraph.", result)
        self.assertTrue(result.endswith("\n\nAfter."))
        self.assertNotIn("\nSecond line.", result)
        self.assertNotIn("\nSecond paragraph.", result)
        self.assertEqual(errors, 0)

    def test_treats_admonition_looking_code_as_literal_content(self) -> None:
        result, errors = self.format("[note]Use [code][tip][/code] literally.[/note]")
        self.assertEqual(result, ".. classref_note::\n\n    Use ``[tip]`` literally.")
        self.assertEqual(errors, 0)

    def test_generates_admonition_directives_from_temporary_xml(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            xml_path = root / "AdmonitionFixture.xml"
            packed_array_xml_path = root / "PackedByteArray.xml"
            output_dir = root / "rst"
            xml_path.write_text(
                '<class name="AdmonitionFixture">\n'
                "  <brief_description>Before. [note]Brief body.[/note] After.</brief_description>\n"
                "  <members>\n"
                '    <member name="bytes" type="PackedByteArray">Byte contents.</member>\n'
                "  </members>\n"
                "</class>\n",
                encoding="utf-8",
            )
            packed_array_xml_path.write_text('<class name="PackedByteArray" />\n', encoding="utf-8")
            output = io.StringIO()
            with mock.patch.object(sys, "argv", ["make_rst.py", str(root), "--output", str(output_dir)]):
                with contextlib.redirect_stdout(output):
                    make_rst.main()

            generated = (output_dir / "class_admonitionfixture.rst").read_text(encoding="utf-8")

        self.assertIn("Before.\n\n.. classref_note::\n\n    Brief body.\n\nAfter.", generated)
        self.assertIn(".. classref_note::\n\n    The returned array is *copied*", generated)
        self.assertIn("PackedByteArray", generated)

    def test_reports_unmatched_closing_admonition(self) -> None:
        _result, errors, diagnostic = self.format_with_diagnostics("[/note]")
        self.assertEqual(errors, 1)
        self.assertEqual(
            diagnostic,
            self.error_diagnostic(
                'TestClass.xml: Closing admonition tag "[/note]" has no opening counterpart in class "TestClass" description.'
            ),
        )

    def test_reports_mismatched_admonition(self) -> None:
        _result, errors, diagnostic = self.format_with_diagnostics("[note]Body.[/warning]")
        self.assertEqual(errors, 1)
        self.assertEqual(
            diagnostic,
            self.error_diagnostic(
                'TestClass.xml: Mismatched closing admonition tag "[/warning]" for "[note]" in class "TestClass" description.'
            ),
        )

    def test_reports_unclosed_admonition(self) -> None:
        _result, errors, diagnostic = self.format_with_diagnostics("[note]Body.")
        self.assertEqual(errors, 1)
        self.assertEqual(
            diagnostic,
            self.error_diagnostic(
                'TestClass.xml: Tag depth mismatch for [note]: no closing [/note] in class "TestClass" description.'
            ),
        )

    def test_reports_nested_admonition(self) -> None:
        _result, errors, diagnostic = self.format_with_diagnostics("[note]Outer [tip]inner[/tip].[/note]")
        self.assertEqual(errors, 1)
        self.assertEqual(
            diagnostic,
            self.error_diagnostic(
                'TestClass.xml: Nested admonition tag "[tip]" in [note] is not supported in class "TestClass" description.'
            ),
        )

    def test_valid_parse_with_same_state_succeeds_after_invalid_parse(self) -> None:
        context, state = self.context()
        with contextlib.redirect_stdout(io.StringIO()):
            make_rst.format_text_block("[note]Unclosed.", context, state)
            result = make_rst.format_text_block("[tip]Valid.[/tip]", context, state)
        self.assertEqual(result, ".. classref_tip::\n\n    Valid.")
        self.assertEqual(state.num_errors, 1)


if __name__ == "__main__":
    unittest.main()
