# Callable/Signal Signature PropertyInfo Encoding — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make typed `Callable`/`Signal` method signatures survive the cross-script `PropertyInfo`/`MethodInfo` boundary by adding a recursively-parseable `PROPERTY_HINT_CALLABLE_TYPE` encoding, so the #327/#382 signature comparators catch nested mismatches between scripts (resolves #412).

**Architecture:** A single new property hint rides on the existing `PropertyInfo.hint`/`hint_string` fields. `DataType::to_property_info` (emit) serializes the signature into a flat string that mirrors `DataType::to_string` surface syntax (`Callable[[int], bool]` → `[[int], bool]`); the encoding is uniformly recursive over `Callable`/`Signal`/`Array`/`Dictionary`. `GDScriptAnalyzer::type_from_property` (parse) reconstructs the nested `DataType` via a recursive-descent decoder. `explicit_callable_type_from_info` / `explicit_signal_type_from_info` and the existing comparators need no change — they already route every slot through `type_from_property`.

**Tech Stack:** C++ (Godot engine), Foundry Script analyzer module, doctest C++ unit tests (`tests/test_macros.h`), Foundry Script `.out` script fixtures.

**Spec:** `docs/superpowers/specs/2026-06-26-callable-signal-signature-property-encoding-design.md`

---

## File Structure

- `core/object/object.h` — add `PROPERTY_HINT_CALLABLE_TYPE` enum value before `PROPERTY_HINT_MAX`.
- `modules/foundry_script/gdscript_parser.cpp` — emit: a recursive signature-type encoder (file-local static helpers) + a `CALLABLE`/`SIGNAL` branch in `DataType::to_property_info`.
- `modules/foundry_script/gdscript_analyzer.cpp` — parse: a recursive signature-type decoder (file-local static helpers) + a `CALLABLE`/`SIGNAL` branch in `GDScriptAnalyzer::type_from_property`; extract a shared leaf-name → `DataType` resolver reused by the existing Array/Dictionary branches.
- `modules/foundry_script/tests/test_gdscript_type.h` — C++ unit tests for the encoder (pure, asserts `hint`/`hint_string`).
- `modules/foundry_script/tests/scripts/analyzer/errors/` and `.../features/` — Foundry Script `.fs` + `.out` fixtures (with `.notest.fs` provider scripts) for the #412 repros, round-trip acceptance, and container-in-signature cases.

---

## Conventions (read once)

- **Build (incremental):** `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)` → binary `bin/godot.macos.editor.dev.<arch>`. (CI parity: swap `dev_build=yes` for `dev_mode=yes` to get warnings-as-errors; do this at least once in Task 5.)
- **Run a single C++ test suite:** `./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Foundry Script*" --force-colors` — but note our memory: a `--test-suite` filter can silently skip suites lacking a matching `TEST_SUITE` macro. Prefer `--test-case="*<name>*"` for a specific case, and run the full `--test` once per task to be safe.
- **Run Foundry Script script fixtures only:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="*Foundry Script*"` runs the script-runner; a failing fixture prints a unified diff of expected vs actual `.out`.
- **Regenerate `.out` fixtures after intentional changes:** `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts`. Always review the diff; never blanket-regenerate.
- **Commit cadence:** one commit per task. End every commit message body with `Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J`.
- **Branch:** work continues on `docs/414-callable-signal-signature-encoding` (the spec commit lives here) or a fresh `feat/414-…` branch off `develop` — confirm with the coordinator before Task 0.

---

### Task 0: Add `PROPERTY_HINT_CALLABLE_TYPE` enum value

**Goal:** Introduce the new property hint without breaking enum binary compatibility or triggering `-Werror` on exhaustive switches.

**Files:**
- Modify: `core/object/object.h` (the `PropertyHint` enum, around `core/object/object.h:95`)

**Acceptance Criteria:**
- [ ] `PROPERTY_HINT_CALLABLE_TYPE` exists, declared immediately before `PROPERTY_HINT_MAX` (appended — no existing value shifts).
- [ ] The editor binary builds clean with no new warnings about unhandled enum cases.

**Verify:** `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)` → build succeeds; `grep -n PROPERTY_HINT_CALLABLE_TYPE core/object/object.h` → one hit.

**Steps:**

- [ ] **Step 1: Add the enum value**

In `core/object/object.h`, change the tail of the `PropertyHint` enum from:

```cpp
	PROPERTY_HINT_INPUT_NAME,
	PROPERTY_HINT_FILE_PATH,
	PROPERTY_HINT_MAX,
};
```

to:

```cpp
	PROPERTY_HINT_INPUT_NAME,
	PROPERTY_HINT_FILE_PATH,
	PROPERTY_HINT_CALLABLE_TYPE, // hint_string carries an encoded Callable/Signal method signature (see Foundry Script DataType::to_property_info).
	PROPERTY_HINT_MAX,
};
```

- [ ] **Step 2: Audit for exhaustive switches that would warn**

Run: `grep -rn "case PROPERTY_HINT_DICTIONARY_TYPE\|case PROPERTY_HINT_ARRAY_TYPE" core editor modules scene servers`
Expected: confirm each such `switch` has a `default:` (today none switch exhaustively over `PropertyHint` without a default; if any new `-Werror=switch` warning appears in Step 3, add the missing case as a graceful fallback, not silent).

- [ ] **Step 3: Build**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: success, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add core/object/object.h
git commit -m "core: add PROPERTY_HINT_CALLABLE_TYPE for callable/signal signatures (#414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

### Task 1: Encode the signature in `DataType::to_property_info`

**Goal:** When a `Callable`/`Signal` `DataType` has an explicit method signature, emit `PROPERTY_HINT_CALLABLE_TYPE` plus a recursively-encoded `hint_string`; untyped callables emit no hint (unchanged).

**Files:**
- Modify: `modules/foundry_script/gdscript_parser.cpp` — add file-local encoder helpers above `DataType::to_property_info` (`modules/foundry_script/gdscript_parser.cpp:6001`); add a branch inside the `BUILTIN` case.
- Test: `modules/foundry_script/tests/test_gdscript_type.h` — encoder unit tests.

**Acceptance Criteria:**
- [ ] `Callable[[int], bool]` → `hint == PROPERTY_HINT_CALLABLE_TYPE`, `hint_string == "[[int], bool]"`, `type == CALLABLE`.
- [ ] `Signal[int]` → `hint_string == "[[int]]"`, `type == SIGNAL` (no return clause).
- [ ] `Callable[[Callable[[int], void]], void]` → `hint_string == "[[Callable[[int], void]], void]"`.
- [ ] `Callable[[Array[int]], void]` → `hint_string == "[[Array[int]], void]"`.
- [ ] An untyped `Callable`/`Signal` (`has_explicit_method_signature == false`) emits `hint == PROPERTY_HINT_NONE` and empty `hint_string`.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="*Callable/Signal property encoding*" --force-colors` → all assertions pass.

**Steps:**

- [ ] **Step 1: Write failing encoder unit tests**

In `modules/foundry_script/tests/test_gdscript_type.h`, inside `namespace GDScriptTests`, add (after the existing helpers, before the closing brace of the namespace):

```cpp
static GDScriptParser::DataType make_typed_array_type(Variant::Type p_element_type) {
	GDScriptParser::DataType type = make_builtin_type(Variant::ARRAY);
	type.set_container_element_type(0, make_builtin_type(p_element_type));
	return type;
}

static GDScriptParser::DataType make_callable_signature_type(
		const Vector<GDScriptParser::DataType> &p_params,
		const GDScriptParser::DataType &p_return,
		bool p_is_signal = false) {
	GDScriptParser::DataType type = make_builtin_type(p_is_signal ? Variant::SIGNAL : Variant::CALLABLE);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_parameter_types = p_params;
	if (!p_is_signal) {
		type.method_return_type.push_back(p_return);
	}
	return type;
}

TEST_CASE("[Modules][Foundry Script] Callable/Signal property encoding") {
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_builtin_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::BOOL)).to_property_info("cb");
		CHECK(info.type == Variant::CALLABLE);
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[int], bool]");
	}
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_builtin_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_variant_type(), true).to_property_info("sig");
		CHECK(info.type == Variant::SIGNAL);
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[int]]");
	}
	{
		// Nested callable parameter.
		Vector<GDScriptParser::DataType> inner_params;
		inner_params.push_back(make_builtin_type(Variant::INT));
		GDScriptParser::DataType inner = make_callable_signature_type(inner_params, make_builtin_type(Variant::NIL));
		Vector<GDScriptParser::DataType> params;
		params.push_back(inner);
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint_string == "[[Callable[[int], void]], void]");
	}
	{
		// Typed-container parameter.
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_typed_array_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint_string == "[[Array[int]], void]");
	}
	{
		// Untyped callable: no hint.
		GDScriptParser::DataType untyped = make_builtin_type(Variant::CALLABLE);
		const PropertyInfo info = untyped.to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_NONE);
		CHECK(info.hint_string.is_empty());
	}
}
```

- [ ] **Step 2: Run tests — confirm they fail**

Run: `./bin/godot.macos.editor.dev.* --headless --test --test-case="*Callable/Signal property encoding*" --force-colors`
Expected: FAIL — `hint` is `PROPERTY_HINT_NONE` and `hint_string` empty for the signature cases (encoder not implemented yet). (You must rebuild first since the test is C++.)

- [ ] **Step 3: Add the file-local encoder helpers**

In `modules/foundry_script/gdscript_parser.cpp`, immediately above `PropertyInfo GDScriptParser::DataType::to_property_info(...)` (line ~6001), add:

```cpp
// Renders a DataType into the flat hint grammar used by PROPERTY_HINT_CALLABLE_TYPE.
// Mirrors DataType::to_string surface syntax, but leaf script/class/enum names use the same
// name selection as PROPERTY_HINT_ARRAY_TYPE so the result round-trips through type_from_property.
static String _encode_signature_leaf_name(const GDScriptParser::DataType &p_type) {
	switch (p_type.kind) {
		case GDScriptParser::DataType::BUILTIN:
			return Variant::get_type_name(p_type.builtin_type);
		case GDScriptParser::DataType::NATIVE:
			return p_type.native_type;
		case GDScriptParser::DataType::SCRIPT:
			if (p_type.script_type.is_valid() && p_type.script_type->get_global_name() != StringName()) {
				return p_type.script_type->get_global_name();
			}
			return p_type.native_type;
		case GDScriptParser::DataType::CLASS:
			if (p_type.class_type != nullptr && p_type.class_type->get_global_name() != StringName()) {
				return p_type.class_type->get_global_name();
			}
			return p_type.native_type;
		case GDScriptParser::DataType::ENUM:
			return String(p_type.native_type).replace("::", ".");
		default:
			return "Variant";
	}
}

static String _encode_signature_type(const GDScriptParser::DataType &p_type);

// Encodes a Callable/Signal signature suffix: "[[p0, p1], ret]" for callables, "[[p0, p1]]" for signals.
static String _encode_method_signature_suffix(const GDScriptParser::DataType &p_type, bool p_has_return) {
	Vector<String> params;
	for (const GDScriptParser::DataType &param : p_type.method_parameter_types) {
		params.push_back(_encode_signature_type(param));
	}
	const String joined = String(", ").join(params);
	if (p_has_return) {
		const GDScriptParser::DataType return_type = p_type.method_return_type.is_empty()
				? GDScriptParser::DataType()
				: p_type.method_return_type[0];
		String return_name;
		if (p_type.method_return_type.is_empty() ||
				(return_type.kind == GDScriptParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL)) {
			return_name = "void";
		} else {
			return_name = _encode_signature_type(return_type);
		}
		return vformat("[[%s], %s]", joined, return_name);
	}
	return vformat("[[%s]]", joined);
}

static String _encode_signature_type(const GDScriptParser::DataType &p_type) {
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::ARRAY:
				if (p_type.has_container_element_type(0)) {
					return vformat("Array[%s]", _encode_signature_type(p_type.get_container_element_type(0)));
				}
				return "Array";
			case Variant::DICTIONARY:
				if (p_type.has_container_element_types()) {
					return vformat("Dictionary[%s, %s]",
							_encode_signature_type(p_type.get_container_element_type_or_variant(0)),
							_encode_signature_type(p_type.get_container_element_type_or_variant(1)));
				}
				return "Dictionary";
			case Variant::CALLABLE:
				if (p_type.has_explicit_method_signature) {
					return "Callable" + _encode_method_signature_suffix(p_type, true);
				}
				return "Callable";
			case Variant::SIGNAL:
				if (p_type.has_explicit_method_signature) {
					return "Signal" + _encode_method_signature_suffix(p_type, false);
				}
				return "Signal";
			default:
				return Variant::get_type_name(p_type.builtin_type);
		}
	}
	return _encode_signature_leaf_name(p_type);
}
```

- [ ] **Step 4: Emit the hint in `to_property_info`**

In `DataType::to_property_info`, inside `case BUILTIN:` (which begins at line ~6012 with `result.type = builtin_type;`), add the following **before** the `if (builtin_type == Variant::ARRAY ...)` check:

```cpp
			result.type = builtin_type;
			if ((builtin_type == Variant::CALLABLE || builtin_type == Variant::SIGNAL) && has_explicit_method_signature) {
				result.hint = PROPERTY_HINT_CALLABLE_TYPE;
				result.hint_string = _encode_method_signature_suffix(*this, builtin_type == Variant::CALLABLE);
			} else if (builtin_type == Variant::ARRAY && has_container_element_type(0)) {
```

(i.e. fold the existing `if (builtin_type == Variant::ARRAY ...)` into the `else if` chain.)

- [ ] **Step 5: Rebuild and run tests — confirm they pass**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu) && ./bin/godot.macos.editor.dev.* --headless --test --test-case="*Callable/Signal property encoding*" --force-colors`
Expected: PASS (all five blocks).

- [ ] **Step 6: Run the full suite to confirm no regression**

Run: `./bin/godot.macos.editor.dev.* --headless --test --force-colors`
Expected: no new failures. (Existing untyped callables still emit no hint, so script fixtures are unaffected.)

- [ ] **Step 7: Commit**

```bash
git add modules/foundry_script/gdscript_parser.cpp modules/foundry_script/tests/test_gdscript_type.h
git commit -m "gdscript: encode Callable/Signal signatures into PropertyInfo hint (#414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

### Task 2: Decode the signature in `GDScriptAnalyzer::type_from_property`

**Goal:** Reconstruct the nested `Callable`/`Signal` `DataType` from `PROPERTY_HINT_CALLABLE_TYPE`, so `explicit_callable_type_from_info` / `explicit_signal_type_from_info` recover full signatures across the script-API boundary.

**Files:**
- Modify: `modules/foundry_script/gdscript_analyzer.cpp` — extract a shared leaf-name resolver from the Array branch; add recursive decoder helpers; add a `CALLABLE`/`SIGNAL` branch in `type_from_property` (`modules/foundry_script/gdscript_analyzer.cpp:8708`).

**Acceptance Criteria:**
- [ ] `type_from_property` on a `CALLABLE` property with `hint == PROPERTY_HINT_CALLABLE_TYPE` returns a `DataType` with `has_explicit_method_signature == true` and reconstructed `method_parameter_types`/`method_return_type`.
- [ ] Nesting is recovered to arbitrary depth (`Callable[[Callable[[int], void]], void]`).
- [ ] `Array[int]` / `Dictionary[String, int]` parameters are recovered with their element types.
- [ ] A `CALLABLE`/`SIGNAL` property with no hint still decodes to a bare (no-signature) callable, exactly as today.

**Verify:** Covered end-to-end by the fixtures in Task 3; this task's direct verification is `./bin/godot.macos.editor.dev.* --headless --test --force-colors` staying green plus the Task 3 fixtures passing.

**Steps:**

- [ ] **Step 1: Extract a shared leaf-name resolver**

The Array branch of `type_from_property` (lines ~8735–8761) resolves a leaf type name through a four-way builtin/native/global-class/enum cascade. Extract it into a file-local static helper so the decoder reuses it. Add near the top of `modules/foundry_script/gdscript_analyzer.cpp` (after the existing file-local helpers, e.g. below `make_signal_type`, ~line 200):

```cpp
// Resolves a single hint type-name (as produced by _encode_signature_leaf_name / PROPERTY_HINT_ARRAY_TYPE)
// into a DataType. Returns false if the name cannot be resolved.
static bool _resolve_hint_leaf_type(const StringName &p_name, GDScriptParser::DataType &r_type) {
	r_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	r_type.is_constant = false;

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_name);
	if (builtin_type < Variant::VARIANT_MAX) {
		r_type.kind = GDScriptParser::DataType::BUILTIN;
		r_type.builtin_type = builtin_type;
		return true;
	}
	if (ClassDB::class_exists(p_name)) {
		r_type.kind = GDScriptParser::DataType::NATIVE;
		r_type.builtin_type = Variant::OBJECT;
		r_type.native_type = p_name;
		return true;
	}
	if (ScriptServer::is_global_class(p_name)) {
		Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(p_name));
		if (script.is_valid()) {
			r_type.kind = GDScriptParser::DataType::SCRIPT;
			r_type.builtin_type = Variant::OBJECT;
			r_type.native_type = script->get_instance_base_type();
			r_type.script_type = script;
			return true;
		}
	}
	return false;
}
```

Note: the existing Array/Dictionary branches use the member `class_exists(...)`; confirm whether it forwards to `ClassDB::class_exists`. If `class_exists` is a member needed here, keep the cascade inline in the decoder instead of a file-local static, or pass the analyzer in. Prefer the smallest change that compiles; if `class_exists` is a non-static member, make `_resolve_hint_leaf_type` a private method of `GDScriptAnalyzer` (declare it in `gdscript_analyzer.h` alongside `type_from_property`).

- [ ] **Step 2: Add the recursive signature decoder helpers**

Add below `_resolve_hint_leaf_type`:

```cpp
// Splits a comma-separated list at bracket depth zero (so nested "Array[int, ...]" / "Callable[[...]]"
// are not split internally). Trims surrounding whitespace on each element.
static Vector<String> _split_signature_top_level(const String &p_text) {
	Vector<String> parts;
	int depth = 0;
	int start = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if (c == '[') {
			depth++;
		} else if (c == ']') {
			depth--;
		} else if (c == ',' && depth == 0) {
			parts.push_back(p_text.substr(start, i - start).strip_edges());
			start = i + 1;
		}
	}
	const String tail = p_text.substr(start).strip_edges();
	if (!tail.is_empty() || !parts.is_empty()) {
		parts.push_back(tail);
	}
	return parts;
}

// Forward declaration; defined after the analyzer method that resolves leaves if needed.
static GDScriptParser::DataType _decode_signature_type(const String &p_encoded);

// Decodes a Callable/Signal signature suffix ("[[p0, p1], ret]" or "[[p0, p1]]") into r_type.
static void _decode_method_signature_suffix(const String &p_suffix, bool p_has_return, GDScriptParser::DataType &r_type) {
	r_type.has_method_signature = true;
	r_type.has_explicit_method_signature = true;
	// p_suffix == "[[<params>], <ret>]" or "[[<params>]]". Strip the outer brackets.
	const String inner = p_suffix.substr(1, p_suffix.length() - 2); // "[<params>], <ret>" or "[<params>]"
	// The parameter list is the first bracket-balanced "[...]" segment.
	int depth = 0;
	int params_end = -1;
	for (int i = 0; i < inner.length(); i++) {
		if (inner[i] == '[') {
			depth++;
		} else if (inner[i] == ']') {
			depth--;
			if (depth == 0) {
				params_end = i;
				break;
			}
		}
	}
	const String params_block = inner.substr(1, params_end - 1); // contents between the inner brackets
	for (const String &param : _split_signature_top_level(params_block)) {
		if (param.is_empty()) {
			continue;
		}
		r_type.method_parameter_types.push_back(_decode_signature_type(param));
	}
	if (p_has_return) {
		// After params_end there is ", <ret>".
		const String rest = inner.substr(params_end + 1).strip_edges();
		const String return_name = rest.begins_with(",") ? rest.substr(1).strip_edges() : rest;
		if (return_name.is_empty() || return_name == "void") {
			GDScriptParser::DataType void_type;
			void_type.kind = GDScriptParser::DataType::BUILTIN;
			void_type.builtin_type = Variant::NIL;
			r_type.method_return_type.push_back(void_type);
		} else {
			r_type.method_return_type.push_back(_decode_signature_type(return_name));
		}
	}
}

static GDScriptParser::DataType _decode_signature_type(const String &p_encoded) {
	GDScriptParser::DataType result;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	const String text = p_encoded.strip_edges();

	if (text.begins_with("Callable[") || text == "Callable") {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::CALLABLE;
		if (text.length() > 8) {
			_decode_method_signature_suffix(text.substr(8), true, result);
		}
		return result;
	}
	if (text.begins_with("Signal[") || text == "Signal") {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::SIGNAL;
		if (text.length() > 6) {
			_decode_method_signature_suffix(text.substr(6), false, result);
		}
		return result;
	}
	if (text.begins_with("Array[")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::ARRAY;
		const String element = text.substr(6, text.length() - 7); // between "Array[" and "]"
		GDScriptParser::DataType element_type = _decode_signature_type(element);
		element_type.is_constant = false;
		result.set_container_element_type(0, element_type);
		return result;
	}
	if (text.begins_with("Dictionary[")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::DICTIONARY;
		const String pair = text.substr(11, text.length() - 12); // between "Dictionary[" and "]"
		const Vector<String> kv = _split_signature_top_level(pair);
		if (kv.size() == 2) {
			GDScriptParser::DataType key_type = _decode_signature_type(kv[0]);
			GDScriptParser::DataType value_type = _decode_signature_type(kv[1]);
			key_type.is_constant = false;
			value_type.is_constant = false;
			result.set_container_element_type(0, key_type);
			result.set_container_element_type(1, value_type);
		}
		return result;
	}
	if (_resolve_hint_leaf_type(text, result)) {
		return result;
	}
	// Unresolvable leaf: degrade to Variant rather than fail the whole decode.
	result.kind = GDScriptParser::DataType::VARIANT;
	return result;
}
```

(If `_resolve_hint_leaf_type` had to become a member per Step 1, thread the analyzer through these helpers or make the whole decoder a private method; keep the recursion structure identical.)

- [ ] **Step 3: Wire the decoder into `type_from_property`**

In `GDScriptAnalyzer::type_from_property` (`modules/foundry_script/gdscript_analyzer.cpp:8708`), in the `else` block where `result.kind = BUILTIN` is set (line ~8733), add a branch alongside the existing `ARRAY`/`DICTIONARY` hint handling:

```cpp
			result.kind = GDScriptParser::DataType::BUILTIN;
			result.builtin_type = p_property.type;
			if ((p_property.type == Variant::CALLABLE || p_property.type == Variant::SIGNAL) &&
					p_property.hint == PROPERTY_HINT_CALLABLE_TYPE && !p_property.hint_string.is_empty()) {
				const GDScriptParser::DataType decoded = _decode_signature_type(
						(p_property.type == Variant::CALLABLE ? "Callable" : "Signal") + p_property.hint_string);
				result.has_method_signature = decoded.has_method_signature;
				result.has_explicit_method_signature = decoded.has_explicit_method_signature;
				result.method_parameter_types = decoded.method_parameter_types;
				result.method_return_type = decoded.method_return_type;
			} else if (p_property.type == Variant::ARRAY && p_property.hint == PROPERTY_HINT_ARRAY_TYPE) {
```

(Fold the existing `if (p_property.type == Variant::ARRAY ...)` into the `else if` chain.)

- [ ] **Step 4: Build**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: success. Resolve any `class_exists`/header-visibility issues per the Step 1 note.

- [ ] **Step 5: Run the full suite — confirm no regression**

Run: `./bin/godot.macos.editor.dev.* --headless --test --force-colors`
Expected: green. (Behavior is observable in Task 3 fixtures; this task must not regress anything.)

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/gdscript_analyzer.cpp modules/foundry_script/gdscript_analyzer.h
git commit -m "gdscript: decode Callable/Signal signatures from PropertyInfo hint (#414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

### Task 3: Cross-script fixtures — #412 repros, round-trip, container-in-signature

**Goal:** Prove the end-to-end behavior: both #412 repros now error, a matching cross-script signature is accepted, and container-typed parameters survive the boundary.

**Files:**
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/external_callable_signature_value_boundary.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/external_callable_signature_value_boundary_provider.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/external_callable_signature_nested_reference.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/external_callable_signature_nested_reference_provider.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/external_callable_signature_roundtrip.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/external_callable_signature_roundtrip_provider.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/external_callable_signature_container_param.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/external_callable_signature_container_param_provider.notest.fs`

**Acceptance Criteria:**
- [ ] Value-boundary repro errors with a Callable type-mismatch on assignment.
- [ ] Nested-callable-under-function-reference repro errors.
- [ ] Matching cross-script signature analyzes clean (feature fixture, no error).
- [ ] `Callable[[Array[int]], void]` mismatch across scripts is caught (e.g. `Array[String]` param).

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="*Foundry Script*" --force-colors` → the four new fixtures pass (expected `.out` matches actual).

**Steps:**

- [ ] **Step 1: Value-boundary provider**

Create `.../errors/external_callable_signature_value_boundary_provider.notest.fs`:

```gdscript
func get_cb() -> Callable[[Callable[[int], void]], void]:
	return func(_inner: Callable[[int], void]) -> void:
		pass
```

- [ ] **Step 2: Value-boundary consumer + expected output**

Create `.../errors/external_callable_signature_value_boundary.fs`:

```gdscript
const Provider = preload("external_callable_signature_value_boundary_provider.notest.fs")

func test() -> void:
	var handler: Callable[[Callable[[String], void]], void] = Provider.new().get_cb()
	print(handler)
```

Create `.../errors/external_callable_signature_value_boundary.out`:

```
GDTEST_ANALYZER_ERROR
>> ERROR at line 4: Cannot assign a value of type Callable[[Callable[[int], void]], void] to variable "handler" with specified type Callable[[Callable[[String], void]], void].
```

> The exact wording/line must match what the analyzer emits. After implementing, run `--gdscript-generate-tests` and inspect the produced `.out` (Step 7); reconcile the hand-written expectation with the real diagnostic rather than forcing the engine to match this text.

- [ ] **Step 3: Nested-reference provider**

Create `.../errors/external_callable_signature_nested_reference_provider.notest.fs`:

```gdscript
func get_cb() -> Callable[[Callable[[int], void]], void]:
	return func(_inner: Callable[[int], void]) -> void:
		pass
```

- [ ] **Step 4: Nested-reference consumer + expected output**

Create `.../errors/external_callable_signature_nested_reference.fs` (exercises the function-reference path — `Provider.new().get_cb` as a value, not a call):

```gdscript
const Provider = preload("external_callable_signature_nested_reference_provider.notest.fs")

func test() -> void:
	var provider := Provider.new()
	var fn: Callable[[Callable[[String], void]], void] = provider.get_cb()
	print(fn)
```

Create the `.out` analogous to Step 2 (reconcile wording in Step 7).

- [ ] **Step 5: Round-trip acceptance fixture (feature, no error)**

Create `.../features/external_callable_signature_roundtrip_provider.notest.fs`:

```gdscript
func get_cb() -> Callable[[Callable[[int], void]], void]:
	return func(_inner: Callable[[int], void]) -> void:
		pass
```

Create `.../features/external_callable_signature_roundtrip.fs`:

```gdscript
const Provider = preload("external_callable_signature_roundtrip_provider.notest.fs")

func test() -> void:
	var handler: Callable[[Callable[[int], void]], void] = Provider.new().get_cb()
	print(handler.is_valid())
```

Create `.../features/external_callable_signature_roundtrip.out`:

```
GDTEST_OK
false
```

> Confirm the printed runtime value via Step 7 generation; the point is that the analyzer raises **no** error (matching signatures accept).

- [ ] **Step 6: Container-in-signature fixture**

Create `.../features/external_callable_signature_container_param_provider.notest.fs`:

```gdscript
func get_cb() -> Callable[[Array[int]], void]:
	return func(_items: Array[int]) -> void:
		pass
```

Create `.../features/external_callable_signature_container_param.fs` as an error fixture instead (move to `errors/` and add `.out`) to prove the `Array[int]` vs `Array[String]` element type survives:

```gdscript
const Provider = preload("external_callable_signature_container_param_provider.notest.fs")

func test() -> void:
	var handler: Callable[[Array[String]], void] = Provider.new().get_cb()
	print(handler)
```

> Decide error-vs-feature placement by what you want to assert. The container fixture is most valuable as an **error** case (mismatched element type caught), so place it under `errors/` with a matching `.out`. Keep one feature fixture (Step 5) for the clean-accept path.

- [ ] **Step 7: Generate `.out`, reconcile, and run**

Run: `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts`
Then `git diff` the new `.out` files; verify the diagnostics are exactly the nested-signature mismatches you intended (not, e.g., a shallow outer-Callable mismatch or an unrelated error). Edit the consumer fixtures if the diagnostic points at the wrong thing. Re-run:
`./bin/godot.macos.editor.dev.* --headless --test --test-case="*Foundry Script*" --force-colors`
Expected: all new fixtures pass.

- [ ] **Step 8: Commit**

```bash
git add modules/foundry_script/tests/scripts/analyzer/
git commit -m "gdscript: tests for cross-script Callable/Signal signature preservation (#412, #414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

### Task 4: Signal-path fixture + bare-callable regression guard

**Goal:** Cover the `Signal` arm of the boundary (signals omit the return clause) and lock in that an untyped cross-script callable is still accepted against any signature (no over-tightening).

**Files:**
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/external_signal_signature_mismatch.fs` + `.out` + `..._provider.notest.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/external_untyped_callable_unchanged.fs` + `.out` + `..._provider.notest.fs`

**Acceptance Criteria:**
- [ ] A typed `Signal[int]` exposed by one script, consumed as `Signal[String]` in another, errors.
- [ ] An untyped `Callable` returned across scripts still assigns to a typed `Callable[[int], void]` variable without error (gradual-typing accept preserved).

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="*Foundry Script*" --force-colors` → both fixtures pass.

**Steps:**

- [ ] **Step 1: Signal mismatch provider**

Create `.../errors/external_signal_signature_mismatch_provider.notest.fs`:

```gdscript
signal pinged(value: int)

func get_signal() -> Signal[int]:
	return pinged
```

- [ ] **Step 2: Signal mismatch consumer**

Create `.../errors/external_signal_signature_mismatch.fs`:

```gdscript
const Provider = preload("external_signal_signature_mismatch_provider.notest.fs")

func test() -> void:
	var s: Signal[String] = Provider.new().get_signal()
	print(s)
```

Create the `.out` (reconcile wording via generation, as in Task 3 Step 7).

- [ ] **Step 3: Untyped-callable regression provider + consumer**

Create `.../features/external_untyped_callable_unchanged_provider.notest.fs`:

```gdscript
func get_cb() -> Callable:
	return func(_x): pass
```

Create `.../features/external_untyped_callable_unchanged.fs`:

```gdscript
const Provider = preload("external_untyped_callable_unchanged_provider.notest.fs")

func test() -> void:
	var typed: Callable[[int], void] = Provider.new().get_cb()
	print(typed.is_valid())
```

Create `.../features/external_untyped_callable_unchanged.out`:

```
GDTEST_OK
false
```

> The point: an untyped source carries no hint, decodes to a bare callable, and remains gradual-typing-compatible. If the analyzer instead errors here, the encoder is wrongly emitting a hint for non-explicit signatures — fix Task 1's `has_explicit_method_signature` guard.

- [ ] **Step 4: Generate, reconcile, run**

Run: `./bin/godot.macos.editor.dev.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts` then
`./bin/godot.macos.editor.dev.* --headless --test --test-case="*Foundry Script*" --force-colors`
Expected: both fixtures pass.

- [ ] **Step 5: Commit**

```bash
git add modules/foundry_script/tests/scripts/analyzer/
git commit -m "gdscript: signal-path + untyped-callable regression fixtures (#414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

### Task 5: Ripple review — Inspector/EditorHelp/docs graceful fallback + CI-parity build

**Goal:** Confirm the new hint degrades gracefully in tools that don't understand it, generated docs are unaffected, and the change passes the CI warnings-as-errors build.

**Files:**
- Inspect (no functional change expected): `editor/editor_help.cpp`, `editor/inspector/editor_inspector.cpp` (hint handling), `modules/foundry_script/editor/gdscript_docgen.cpp`.
- Possibly modify: one of the above only if a new `PropertyHint` value produces a crash/garbage rather than a graceful fallback.

**Acceptance Criteria:**
- [ ] Inspector and `EditorHelp` render a property carrying `PROPERTY_HINT_CALLABLE_TYPE` as a plain `Callable`/`Signal` (no crash, no garbage) — verified by code inspection of the hint-dispatch (default/fallback path) plus a smoke run.
- [ ] `--gdscript-generate-tests` and doc generation produce no unexpected diffs beyond the new fixtures.
- [ ] CI-parity build (`dev_mode=yes`) compiles with warnings-as-errors.

**Verify:** `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)` succeeds; `./bin/godot.macos.editor.dev.* --headless --test --force-colors` fully green.

**Steps:**

- [ ] **Step 1: Audit hint consumers**

Run: `grep -rn "PROPERTY_HINT_ARRAY_TYPE\|PROPERTY_HINT_DICTIONARY_TYPE" editor modules/foundry_script/editor scene`
For each site, confirm an unknown/默认 hint falls through to plain-type rendering. Note any site that assumes `hint_string` parses a specific way for `CALLABLE`/`SIGNAL` types (there should be none today, since callables never carried a hint). If a site would misread the new `hint_string`, add an explicit guard so it ignores `PROPERTY_HINT_CALLABLE_TYPE`.

- [ ] **Step 2: Doc-gen check**

Run: `grep -rn "hint" modules/foundry_script/editor/gdscript_docgen.cpp` and confirm doc generation does not serialize the callable `hint_string` into class XML in a way that changes output for typed callables. If Foundry Script doc-gen already omits callable signatures from docs, no change is needed.

- [ ] **Step 3: CI-parity build**

Run: `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: clean compile (warnings-as-errors). Fix any `-Wshadow`/`-Wswitch` issues (recall: macOS clang can miss Linux `-Wshadow`; avoid shadowing names in the new helpers).

- [ ] **Step 4: Full test suite**

Run: `./bin/godot.macos.editor.dev.* --headless --test --force-colors`
Expected: fully green, including `test_class_db` (run the unfiltered suite, not a `--test-suite` filter).

- [ ] **Step 5: Smoke-run the editor (optional, if display available)**

Open a scene/script exposing a typed callable property and confirm the Inspector shows it without error. On a headless VM this is skippable; note it as manually unverified if so.

- [ ] **Step 6: Commit (only if Steps 1–2 required a guard)**

```bash
git add editor/ modules/foundry_script/editor/
git commit -m "editor: ignore PROPERTY_HINT_CALLABLE_TYPE in hint consumers for graceful fallback (#414)

Claude-Session: https://claude.ai/code/session_016mpDPJgtz4gxEsB2QMeS8J"
```

---

## Self-Review Notes

- **Spec coverage:** enum (Task 0) · emit (Task 1) · parse (Task 2) · #412 repros + container recursion (Task 3) · signal arm + untyped regression (Task 4) · ripple review/Inspector/docs/CI parity (Task 5). The "no comparator change" claim is validated implicitly by Task 3/4 passing without touching the comparators.
- **Open implementation risk flagged in-plan:** whether `class_exists` is a member vs `ClassDB::class_exists` (Task 2 Step 1) determines if the decoder can be file-local static or must be a private method. The plan tells the implementer to take the smallest compiling change and keep the recursion identical.
- **`.out` wording:** every error fixture is reconciled against generated output (Task 3 Step 7 / Task 4 Step 4) rather than asserting hand-guessed diagnostic text — the assertion that matters is *which* mismatch is reported, not its exact prose.
- **Degradation guard:** Task 4 Step 3 is the canary for the most likely bug — emitting a hint for non-explicit callables would break gradual typing; the untyped-callable fixture fails loudly if so.
