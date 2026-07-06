#!/usr/bin/env python3

from __future__ import annotations

import ast
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCONSTRUCT = ROOT / "SConstruct"


def _is_common_warnings_target(node: ast.AST) -> bool:
    return isinstance(node, ast.Name) and node.id == "common_warnings"


def _contains_literal_string(node: ast.AST, value: str) -> bool:
    return any(isinstance(child, ast.Constant) and child.value == value for child in ast.walk(node))


def _condition_uses_clang_warning_branch(node: ast.AST) -> bool:
    for child in ast.walk(node):
        if not isinstance(child, ast.Call):
            continue
        if not isinstance(child.func, ast.Attribute):
            continue
        if not isinstance(child.func.value, ast.Name) or child.func.value.id != "methods":
            continue
        if child.func.attr == "using_clang":
            return True
    return False


def _body_adds_common_warning(body: list[ast.stmt], warning: str) -> bool:
    for statement in ast.walk(ast.Module(body=body, type_ignores=[])):
        if not isinstance(statement, ast.AugAssign):
            continue
        if not isinstance(statement.op, ast.Add):
            continue
        if not _is_common_warnings_target(statement.target):
            continue
        if _contains_literal_string(statement.value, warning):
            return True
    return False


def main() -> int:
    tree = ast.parse(SCONSTRUCT.read_text(encoding="utf-8"), filename=str(SCONSTRUCT))

    for node in ast.walk(tree):
        if not isinstance(node, ast.If):
            continue
        if not _condition_uses_clang_warning_branch(node.test):
            continue
        if _body_adds_common_warning(node.body, "-Wshadow"):
            return 0

    print("SConstruct must enable -Wshadow in the Clang common warning branch.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
