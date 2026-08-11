#!/usr/bin/env python3
"""Behavioral coverage for documentation admonition conversion."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
import unittest
from pathlib import Path
from typing import Any

_MODULE_PATH = Path(__file__).resolve().parents[2] / "doc" / "tools" / "make_rst.py"
_spec = importlib.util.spec_from_file_location("make_rst", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
make_rst: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = make_rst
_spec.loader.exec_module(make_rst)


class MakeRstAdmonitionTests(unittest.TestCase):
    def format(self, text: str) -> tuple[str, int]:
        state = make_rst.State()
        state.current_class = "TestClass"
        context = make_rst.ClassDef("TestClass")
        with contextlib.redirect_stdout(io.StringIO()):
            result = make_rst.format_text_block(text, context, state)
        return result, state.num_errors

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

    def test_reports_unmatched_closing_admonition(self) -> None:
        _result, errors = self.format("[/note]")
        self.assertEqual(errors, 1)

    def test_reports_mismatched_admonition(self) -> None:
        _result, errors = self.format("[note]Body.[/warning]")
        self.assertEqual(errors, 1)

    def test_reports_unclosed_admonition(self) -> None:
        _result, errors = self.format("[note]Body.")
        self.assertEqual(errors, 1)

    def test_reports_nested_admonition(self) -> None:
        _result, errors = self.format("[note]Outer [tip]inner[/tip].[/note]")
        self.assertEqual(errors, 1)


if __name__ == "__main__":
    unittest.main()
