# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.

import os

import common
import pytest
import rename

FIXTURE = os.path.join(os.path.dirname(__file__), "fixtures", "rules.tsv")


def code_rules():
    return rename.select_rules(rename.load_rules(FIXTURE), ("code", "both"))


def prose_rules():
    return rename.select_rules(rename.load_rules(FIXTURE), ("prose", "both"))


def test_load_rules_reads_all_columns():
    rows = rename.load_rules(FIXTURE)
    parser = next(r for r in rows if r["from"] == "GDScriptParser")
    assert parser["to"] == "FSParser"
    assert parser["context"] == "code"


def test_longest_match_first():
    out = rename.replace_text("GDScriptParser uses GDScript", code_rules())
    assert out == "FSParser uses FoundryScript"


def test_word_boundary_safety():
    # GDScript inside a larger identifier is left alone (no exact rule for it).
    out = rename.replace_text("class MyGDScriptHelper {};", code_rules())
    assert out == "class MyGDScriptHelper {};"


def test_prefix_rule_underscore_suffixed():
    # A rule whose source ends in "_" matches the leading run of a snake_case
    # token, so a whole identifier/include family is renamed by one rule.
    rules = code_rules()
    assert rename.replace_text("gdscript_parser", rules) == "fs_parser"
    assert (
        rename.replace_text('#include "modules/gdscript/gdscript_parser.h"', rules)
        == '#include "modules/foundry_script/fs_parser.h"'
    )
    # The leading word boundary still holds: a prefix rule never matches
    # mid-token, so "my_gdscript_path" is left untouched.
    assert rename.replace_text("my_gdscript_path", rules) == "my_gdscript_path"


def test_code_vs_prose_mapping():
    assert rename.replace_text("GDScript", code_rules()) == "FoundryScript"
    assert rename.replace_text("GDScript", prose_rules()) == "Foundry Script"


def test_prose_phrase_literal_match():
    assert rename.replace_text("the Godot Engine rocks", prose_rules()) == "the Foundry rocks"


def test_extension_rule_anchored_does_not_eat_gdshader():
    rules = code_rules()
    assert rename.replace_text("res://a.gd", rules) == "res://a.fs"
    # ".gd" must not corrupt ".gdshader".
    assert rename.replace_text("res://a.gdshader", rules) == "res://a.gdshader"
    # Longer extension rule wins over ".gd".
    assert rename.replace_text("mod.gdextension", rules) == "mod.foundryextension"


def test_macro_replacement():
    out = rename.replace_text("GDCLASS(Foo, Object)", code_rules())
    assert out == "FOUNDRY_CLASS(Foo, Object)"


def test_thirdparty_and_git_excluded():
    assert rename.is_excluded("thirdparty/zlib/zlib.h")
    assert rename.is_excluded(".git/config")
    assert rename.is_excluded("modules/x/.git/info")
    # Nested vendor trees must be excluded, not just the top-level thirdparty/.
    assert rename.is_excluded("modules/mono/thirdparty/cli/x.cpp")
    assert not rename.is_excluded("core/object/object.h")
    # A filename that merely contains "thirdparty" as a substring is not a dir.
    assert not rename.is_excluded("core/thirdparty_notes.h")


def test_exclusion_predicate_is_unified():
    # generate_map and rename must share one definition of scope.
    assert rename.is_excluded is common.is_excluded
    import generate_map

    assert generate_map.iter_tracked_files is common.iter_tracked_files


def test_idempotence():
    rules = code_rules()
    text = "GDScriptParser GDScript GDExtensionManager GDCLASS res://a.gd"
    once = rename.replace_text(text, rules)
    twice = rename.replace_text(once, rules)
    assert once == twice


def test_plan_file_moves_extension_and_filename():
    rules = rename.select_rules(rename.load_rules(FIXTURE), rename.MOVE_CONTEXTS)
    paths = [
        "scene/a.gd",
        "project.godot",
        "core/object.cpp",
        "demo/x.gdshader",
        "mod.gdextension",
    ]
    moves = dict(rename.plan_file_moves(paths, rules))
    assert moves["scene/a.gd"] == "scene/a.fs"
    assert moves["project.godot"] == "project.foundry"
    assert moves["mod.gdextension"] == "mod.foundryextension"
    # Unaffected paths produce no move.
    assert "core/object.cpp" not in moves
    assert "demo/x.gdshader" not in moves


def test_plan_file_moves_deepest_first():
    rules = rename.select_rules(rename.load_rules(FIXTURE), rename.MOVE_CONTEXTS)
    paths = ["a.gd", "deep/nested/b.gd", "one/c.gd"]
    moves = rename.plan_file_moves(paths, rules)
    depths = [src.count("/") for src, _ in moves]
    assert depths == sorted(depths, reverse=True)


def test_moves_ignore_prose_rule():
    # A path literally named like the prose token must not gain a space.
    rules = rename.select_rules(rename.load_rules(FIXTURE), rename.MOVE_CONTEXTS)
    moves = dict(rename.plan_file_moves(["dir/GDScript"], rules))
    # "GDScript" maps via the code rule to "FoundryScript", never "Foundry Script".
    assert moves["dir/GDScript"] == "dir/FoundryScript"


def test_content_pass_preserves_crlf(tmp_path):
    rules = rename.load_rules(FIXTURE)
    crlf = tmp_path / "win.props"
    crlf.write_bytes(b"GDScript one\r\nGDScript two\r\n")
    lf = tmp_path / "unix.cpp"
    lf.write_bytes(b"GDScript one\nGDScript two\n")
    rename.run_content_pass(str(tmp_path), rules, ("code", "both"), ["win.props", "unix.cpp"], dry_run=False)
    # CRLF survives exactly; LF stays LF (no spurious CR introduced).
    assert crlf.read_bytes() == b"FoundryScript one\r\nFoundryScript two\r\n"
    assert lf.read_bytes() == b"FoundryScript one\nFoundryScript two\n"


def test_content_pass_dry_run_writes_nothing(tmp_path):
    rules = rename.load_rules(FIXTURE)
    target = tmp_path / "a.cpp"
    original = b"GDScript x\r\n"
    target.write_bytes(original)
    rename.run_content_pass(str(tmp_path), rules, ("code", "both"), ["a.cpp"], dry_run=True)
    assert target.read_bytes() == original


def test_move_collision_duplicate_destination(tmp_path):
    with pytest.raises(ValueError):
        rename.check_move_collisions([("a.gd", "x.fs"), ("b.gd", "x.fs")], str(tmp_path))


def test_move_collision_existing_target(tmp_path):
    (tmp_path / "exists.fs").write_text("present")
    with pytest.raises(ValueError):
        rename.check_move_collisions([("a.gd", "exists.fs")], str(tmp_path))


def test_move_collisions_clean_plan_ok(tmp_path):
    # Distinct destinations that do not pre-exist: no error.
    rename.check_move_collisions([("a.gd", "a.fs"), ("b.gd", "b.fs")], str(tmp_path))


def test_move_target_that_is_also_a_source_ok(tmp_path):
    # "a.fs" exists but is itself being moved away, so it is not a clobber.
    (tmp_path / "a.fs").write_text("present")
    rename.check_move_collisions([("a.fs", "b.fs"), ("c.gd", "a.fs")], str(tmp_path))
