# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.

import subprocess

import generate_map as gm

# Real GDVIRTUAL / GDREGISTER spellings harvested from the frozen tree; the
# classifier must round-trip every one of them without raising.
GDVIRTUAL_FORMS = [
    "GDVIRTUAL_BIND",
    "GDVIRTUAL_BIND_COMPAT",
    "GDVIRTUAL_CALL",
    "GDVIRTUAL_CALL_",
    "GDVIRTUAL_CALL_PTR",
    "GDVIRTUAL_FUNC_ADDR",
    "GDVIRTUAL_IS_OVERRIDDEN",
    "GDVIRTUAL_IS_OVERRIDDEN_PTR",
    "GDVIRTUAL_NATIVE_PTR",
    "GDVIRTUAL0",
    "GDVIRTUAL0_REQUIRED",
    "GDVIRTUAL0R",
    "GDVIRTUAL0R_COMPAT",
    "GDVIRTUAL0R_REQUIRED",
    "GDVIRTUAL0RC",
    "GDVIRTUAL0RC_REQUIRED",
    "GDVIRTUAL1",
    "GDVIRTUAL1C",
    "GDVIRTUAL1R_COMPAT",
    "GDVIRTUAL10R_REQUIRED",
    "GDVIRTUAL3_COMPAT",
    "GDVIRTUAL6C_COMPAT",
    "GDVIRTUAL9R_REQUIRED",
]
GDREGISTER_FORMS = [
    "GDREGISTER_ABSTRACT_CLASS",
    "GDREGISTER_CLASS",
    "GDREGISTER_INTERNAL_CLASS",
    "GDREGISTER_NATIVE_STRUCT",
    "GDREGISTER_RUNTIME_CLASS",
    "GDREGISTER_VIRTUAL_CLASS",
]


def test_classify_gdscript_family():
    assert gm.classify_token("GDScript") == ("FoundryScript", "A")
    assert gm.classify_token("GDScriptParser") == ("FSParser", "A")
    assert gm.classify_token("GDScriptLanguageServer") == ("FSLanguageServer", "A")


def test_classify_gdextension_family():
    assert gm.classify_token("GDExtension") == ("FoundryExtension", "B")
    assert gm.classify_token("GDExtensionManager") == ("FoundryExtensionManager", "B")


def test_classify_macros():
    assert gm.classify_token("GDCLASS") == ("FOUNDRY_CLASS", "C")
    assert gm.classify_token("GDSOFTCLASS") == ("FOUNDRY_SOFTCLASS", "C")
    assert gm.classify_token("GDVIRTUAL0") == ("FOUNDRY_VIRTUAL0", "C")
    assert gm.classify_token("GDREGISTER_ABSTRACT_CLASS") == ("FOUNDRY_REGISTER_ABSTRACT_CLASS", "C")


def test_classify_godot_macro():
    assert gm.classify_token("GODOT_VERSION") == ("FOUNDRY_VERSION", "D")


def test_classify_round_trips_real_macro_forms():
    for token in GDVIRTUAL_FORMS:
        to, category = gm.classify_token(token)
        assert category == "C"
        assert to == "FOUNDRY_VIRTUAL" + token[len("GDVIRTUAL") :]
    for token in GDREGISTER_FORMS:
        to, category = gm.classify_token(token)
        assert category == "C"
        assert to == "FOUNDRY_REGISTER_" + token[len("GDREGISTER_") :]


def test_unclassifiable_token_raises():
    import pytest

    with pytest.raises(ValueError):
        gm.classify_token("NotAToken")


def test_token_regex_is_word_boundary_safe():
    tokens = gm.scan_text("class MyGDScriptHelper : GDScriptParser {};")
    assert "GDScriptParser" in tokens
    # The GDScript inside MyGDScriptHelper must not surface as its own token.
    assert "GDScript" not in tokens
    assert not any(t.endswith("Helper") for t in tokens)


def test_scan_text_discovers_families():
    text = "GDCLASS(Foo, Object)\nGDVIRTUAL1R_REQUIRED(int, do_it)\nGDExtensionManager *m;\n#define GODOT_VERSION 4\n"
    tokens = gm.scan_text(text)
    assert {"GDCLASS", "GDVIRTUAL1R_REQUIRED", "GDExtensionManager", "GODOT_VERSION"} <= tokens


def test_seed_context_inference():
    assert gm.seed_context("GDScript", "Foundry Script") == "prose"
    assert gm.seed_context("Godot Engine", "Foundry") == "prose"
    assert gm.seed_context(".gd", ".fs") == "both"
    assert gm.seed_context("project.godot", "project.foundry") == "both"
    assert gm.seed_context("godotengine.org", "cafecito.games") == "both"
    assert gm.seed_context("GDScriptParser", "FSParser") == "code"


def _row(from_, to, category="A", context="code", notes=""):
    return {"from": from_, "to": to, "category": category, "context": context, "notes": notes}


def test_prose_rule_is_additive_to_code_rule():
    generated = [_row("GDScript", "FoundryScript", "A", "code")]
    seed = [_row("GDScript", "Foundry Script", "D", "prose")]
    merged = gm.merge_seed(generated, seed)
    code = [r for r in merged if r["from"] == "GDScript" and r["context"] == "code"]
    prose = [r for r in merged if r["from"] == "GDScript" and r["context"] == "prose"]
    assert len(code) == 1 and code[0]["to"] == "FoundryScript"
    assert len(prose) == 1 and prose[0]["to"] == "Foundry Script"


def test_seed_wins_same_token_same_context():
    generated = [_row("GDScriptParser", "WRONG", "A", "code")]
    seed = [_row("GDScriptParser", "FSParser", "A", "code")]
    merged = gm.merge_seed(generated, seed)
    rows = [r for r in merged if r["from"] == "GDScriptParser"]
    assert len(rows) == 1 and rows[0]["to"] == "FSParser"


def test_exclusion_drops_generated_row():
    generated = [_row("GDScriptDocGen", "FSDocGen", "A", "code")]
    seed = [_row("GDScriptDocGen", gm.EXCLUDE_MARKER)]
    merged = gm.merge_seed(generated, seed)
    assert not any(r["from"] == "GDScriptDocGen" for r in merged)


def test_output_sorted_longest_source_first():
    generated = [
        _row("GDScript", "FoundryScript"),
        _row("GDScriptParser", "FSParser"),
        _row("GDScriptLanguageServer", "FSLanguageServer"),
    ]
    merged = gm.merge_seed(generated, [])
    lengths = [len(r["from"]) for r in merged]
    assert lengths == sorted(lengths, reverse=True)


def test_scan_excludes_nested_thirdparty(tmp_path):
    # The generator-side scan must skip nested vendor trees, not just top-level.
    root = str(tmp_path)
    subprocess.check_call(["git", "init", "-q", root])
    (tmp_path / "core").mkdir()
    (tmp_path / "core" / "a.cpp").write_text("GDScriptParser p;\n")
    nested = tmp_path / "modules" / "mono" / "thirdparty"
    nested.mkdir(parents=True)
    (nested / "b.cpp").write_text("GDExtensionManager m;\n")
    subprocess.check_call(["git", "add", "-A"], cwd=root)

    tokens = gm.scan_tokens(root)
    assert "GDScriptParser" in tokens
    assert "GDExtensionManager" not in tokens
