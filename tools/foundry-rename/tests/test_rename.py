# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.

import os

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
    assert not rename.is_excluded("core/object/object.h")


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
