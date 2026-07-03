# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_SCRIPT = REPO_ROOT / "misc/scripts/copyright_headers.py"
CREATE_TEST_SCRIPT = REPO_ROOT / "tests/create_test.py"


def run_script(script: Path, *args: Path | str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, str(script), *[str(arg) for arg in args]],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return result


def test_new_file_gets_foundry_engine_header(tmp_path: Path) -> None:
    source = tmp_path / "net_new.cpp"
    source.write_text("int answer() { return 42; }\n", encoding="utf-8")

    run_script(HEADER_SCRIPT, source)

    text = source.read_text(encoding="utf-8")
    assert "FOUNDRY ENGINE" in text
    assert "A fork of the Godot Engine" in text
    assert "Copyright (c) 2026-present Cafecito Games LLC." in text
    assert "Godot Engine contributors" not in text
    assert "Juan Linietsky" not in text
    assert text.endswith("int answer() { return 42; }\n")


def test_inherited_godot_header_does_not_gain_cafecito_copyright(tmp_path: Path) -> None:
    source = tmp_path / "inherited.cpp"
    source.write_text(
        """/**************************************************************************/
/*  inherited.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/**************************************************************************/

int inherited() { return 1; }
""",
        encoding="utf-8",
    )

    run_script(HEADER_SCRIPT, source)

    text = source.read_text(encoding="utf-8")
    assert "GODOT ENGINE" in text
    assert "Godot Engine contributors" in text
    assert "Juan Linietsky" in text
    assert "Cafecito Games LLC" not in text
    assert text.endswith("int inherited() { return 1; }\n")


def test_create_test_uses_foundry_engine_header(tmp_path: Path) -> None:
    run_script(CREATE_TEST_SCRIPT, "HeaderPolicy", tmp_path)

    text = (tmp_path / "test_header_policy.h").read_text(encoding="utf-8")
    assert "FOUNDRY ENGINE" in text
    assert "A fork of the Godot Engine" in text
    assert "Copyright (c) 2026-present Cafecito Games LLC." in text
    assert "Godot Engine contributors" not in text
    assert "Juan Linietsky" not in text
