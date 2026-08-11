# Upstream Editor Improvements Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port semantic documentation callouts, ranged Foundry Script diagnostic squiggles, and the CanvasItem cached-viewport optimization into Foundry with regression coverage.

**Architecture:** Three workers own disjoint change sets: documentation rendering/generation, script diagnostics/TextEdit decorations, and CanvasItem viewport lookup. Upstream PR heads in `refs/upstream-pr/{111375,119588,111086}` are references only; each worker adapts the behavior to current Foundry APIs and validates observable behavior before handoff.

**Tech Stack:** Foundry Engine C++, doctest, Foundry Script parser/analyzer, Python `unittest`, editor automation MCP, SCons/Ninja through `scripts/agent_build.py`.

---

## File Ownership

### Documentation worker

- `doc/tools/make_rst.py`
- `editor/doc/editor_help.cpp`
- `editor/doc/editor_help.h`
- `editor/themes/editor_theme_manager.h`
- `editor/themes/theme_classic.cpp`
- `editor/themes/theme_modern.cpp`
- `scripts/tests/test_make_rst_admonitions.py`
- `tests/editor/test_editor_help_type_links.h`

### Diagnostics worker

- `core/object/script_language.h`
- `core/object/script_language_extension.h`
- `doc/classes/EditorSettings.xml`
- `editor/script/script_text_editor.cpp`
- `editor/script/script_text_editor.h`
- `editor/settings/editor_settings.cpp`
- `editor/themes/editor_theme_manager.cpp`
- `modules/foundry_script/fs_editor.cpp`
- `modules/foundry_script/fs_parser.cpp`
- `modules/foundry_script/fs_parser.h`
- `modules/foundry_script/fs_warning.h`
- `scene/gui/text_edit.cpp`
- `scene/gui/text_edit.h`
- `modules/foundry_script/tests/test_foundry_script.cpp`
- `tests/scene/test_text_edit.h`

### CanvasItem worker

- `scene/main/canvas_item.cpp`
- `tests/scene/test_node_2d.h`

Workers must not edit files outside their ownership without coordinating with the root agent.

## Task 1: Restore CanvasItem Cached Viewport Lookup

**Files:**

- Modify: `tests/scene/test_node_2d.h`
- Modify: `scene/main/canvas_item.cpp`

- [ ] **Step 1: Add behavior characterization for Window and SubViewport parents**

Add a doctest that constructs a `Node2D` below a non-CanvasItem intermediary in a `Window`, verifies `get_viewport()` returns that Window, and verifies the item follows Window visibility. Add a second subcase below a `SubViewport` and verify the item remains visible in tree.

```cpp
TEST_CASE("[SceneTree][Node2D] CanvasItem uses its cached viewport on enter tree") {
	Window *root = SceneTree::get_singleton()->get_root();

	SUBCASE("Window visibility is inherited through a non-CanvasItem parent") {
		Window *window = memnew(Window);
		Node *intermediary = memnew(Node);
		Node2D *item = memnew(Node2D);
		root->add_child(window);
		window->add_child(intermediary);
		intermediary->add_child(item);

		CHECK(item->get_viewport() == window);
		CHECK(item->is_visible_in_tree());
		window->hide();
		CHECK_FALSE(item->is_visible_in_tree());

		memdelete(window);
	}

	SUBCASE("SubViewport keeps CanvasItems visible") {
		SubViewport *viewport = memnew(SubViewport);
		Node *intermediary = memnew(Node);
		Node2D *item = memnew(Node2D);
		root->add_child(viewport);
		viewport->add_child(intermediary);
		intermediary->add_child(item);

		CHECK(item->get_viewport() == viewport);
		CHECK(item->is_visible_in_tree());

		memdelete(viewport);
	}
}
```

- [ ] **Step 2: Build and run the characterization test**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*CanvasItem uses its cached viewport*"
```

Expected before the optimization: PASS, because the change intentionally preserves behavior. Record this as a performance-only characterization exception to the normal red step.

- [ ] **Step 3: Replace the ancestor walk with the cached viewport**

In `CanvasItem::_notification(NOTIFICATION_ENTER_TREE)`, replace the manual parent walk with:

```cpp
window = Object::cast_to<Window>(get_viewport());
```

Keep the existing Window signal connection, visibility assignment, and SubViewport `parent_visible_in_tree = true` branch unchanged.

- [ ] **Step 4: Rebuild and rerun the focused test**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*CanvasItem uses its cached viewport*"
```

Expected: build succeeds and the focused doctest passes.

- [ ] **Step 5: Commit the isolated change**

```sh
git add scene/main/canvas_item.cpp tests/scene/test_node_2d.h
git commit -m "Optimize CanvasItem viewport lookup"
```

## Task 2: Add Documentation Admonitions

**Files:**

- Modify: `doc/tools/make_rst.py`
- Modify: `editor/doc/editor_help.cpp`
- Modify: `editor/doc/editor_help.h`
- Modify: `editor/themes/editor_theme_manager.h`
- Modify: `editor/themes/theme_classic.cpp`
- Modify: `editor/themes/theme_modern.cpp`
- Create: `scripts/tests/test_make_rst_admonitions.py`
- Modify: `tests/editor/test_editor_help_type_links.h`

- [ ] **Step 1: Write failing Python formatter tests**

Import `doc.tools.make_rst`, create `State()` and `DefinitionBase("description", "Example")`, and assert all four tags produce their corresponding directives. Cover multiline bodies, surrounding prose, unmatched closing tags, and nested admonitions.

```python
class MakeRstAdmonitionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = make_rst.State()
        self.state.current_class = "Example"
        self.context = make_rst.DefinitionBase("description", "Example")

    def test_formats_all_admonition_types(self) -> None:
        for name in ("note", "warning", "tip", "important"):
            with self.subTest(name=name):
                actual = make_rst.format_text_block(
                    f"Before.\n\n[{name}]First line.\nSecond line.[/{name}]\n\nAfter.",
                    self.context,
                    self.state,
                )
                self.assertIn(f".. classref_{name}::", actual)
                self.assertIn("    First line.\n    Second line.", actual)
                self.assertIn("Before.", actual)
                self.assertIn("After.", actual)

    def test_rejects_nested_admonitions(self) -> None:
        make_rst.format_text_block(
            "[note]Outer [warning]inner[/warning][/note]", self.context, self.state
        )
        self.assertGreater(self.state.num_errors, 0)
```

- [ ] **Step 2: Run Python tests and confirm RED**

```sh
python3 -m unittest scripts.tests.test_make_rst_admonitions -v
```

Expected: FAIL because the semantic tags are not recognized or converted.

- [ ] **Step 3: Implement `make_rst.py` conversion**

Adapt the final diff from `refs/upstream-pr/111375`, using Foundry's current tag-depth validator. Add:

```python
RESERVED_ADMONITION_TAGS = ["important", "note", "tip", "warning"]
```

Track one open admonition, convert its stripped body to four-space-indented directive content, preserve blank-line boundaries around surrounding text, and report invalid close/nesting through `print_error`. Convert the packed-array copy message in `BASE_STRINGS` and `make_rst_class()` to `[note]...[/note]`.

- [ ] **Step 4: Run Python tests and confirm GREEN**

```sh
python3 -m unittest scripts.tests.test_make_rst_admonitions -v
```

Expected: all tests pass.

- [ ] **Step 5: Write a failing editor-help rendering test**

Under `TESTS_ENABLED`, expose a wrapper that renders documentation markup into a supplied `RichTextLabel`. Extend `test_editor_help_type_links.h` to render all four tags plus nested `[code]` markup and assert `get_parsed_text()` contains `Note:`, `Warning:`, `Tip:`, `Important:`, the bodies, and surrounding prose.

- [ ] **Step 6: Run the editor-help test and confirm RED**

```sh
./bin/foundry.* --headless test run --case "*EditorHelp*admonition*" --force-colors
```

Expected: FAIL because the renderer ignores the semantic tags.

- [ ] **Step 7: Implement editor rendering and theme colors**

Adapt the final editor/theme portion of `refs/upstream-pr/111375`. Add `info_color` to shared theme configuration, set dark/light variants in classic and modern themes, define the four `EditorHelp` colors, and render the four labels/icons in `_add_text_to_rt()` using the existing tag stack.

- [ ] **Step 8: Rebuild and run documentation tests**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*EditorHelp*admonition*"
python3 -m unittest scripts.tests.test_make_rst_admonitions -v
```

Expected: build and all focused tests pass.

- [ ] **Step 9: Commit the isolated change**

```sh
git add doc/tools/make_rst.py editor/doc/editor_help.cpp editor/doc/editor_help.h \
  editor/themes/editor_theme_manager.h editor/themes/theme_classic.cpp \
  editor/themes/theme_modern.cpp scripts/tests/test_make_rst_admonitions.py \
  tests/editor/test_editor_help_type_links.h
git commit -m "Add editor documentation admonitions"
```

## Task 3: Add Ranged Foundry Script Diagnostic Squiggles

**Files:**

- Modify every file listed under Diagnostics worker ownership.

- [ ] **Step 1: Write failing Foundry Script range tests**

Add tests that call `FSLanguage::validate()` with a known unused-variable warning and a parser error. Assert one-based, end-exclusive ranges:

```cpp
List<ScriptLanguage::ScriptError> errors;
List<ScriptLanguage::Warning> warnings;
const bool valid = FSLanguage::get_singleton()->validate(source, "res://range_test.fs", nullptr, &errors, &warnings);

REQUIRE_FALSE(valid);
REQUIRE_FALSE(errors.is_empty());
CHECK(errors.front()->get().start_line == expected_start_line);
CHECK(errors.front()->get().start_column == expected_start_column);
CHECK(errors.front()->get().end_line == expected_end_line);
CHECK(errors.front()->get().end_column == expected_end_column);
```

Use parser/analyzer inputs whose node/token ranges are already known from existing parser tests rather than hard-coding guessed coordinates.

- [ ] **Step 2: Run range tests and confirm RED**

```sh
./bin/foundry.* --headless test run --case "*FoundryScript*diagnostic range*" --force-colors
```

Expected: compilation or assertion failure because generic diagnostics have no column/end range.

- [ ] **Step 3: Implement generic and Foundry Script ranges**

Use this generic shape:

```cpp
struct Warning {
	int start_line = 0;
	int start_column = -1;
	int end_line = 0;
	int end_column = -1;
	int code;
	String string_code;
	String message;
};

struct ScriptError {
	String path;
	int start_line = -1;
	int start_column = -1;
	int end_line = -1;
	int end_column = -1;
	String message;
};
```

Keep `FSParser::ParserError::line/column` for internal compatibility and add `end_line/end_column`. Populate all four values from the origin node or previous token in `push_error()`. Add columns to `FSWarning` from the pending warning's source node. Translate these ranges in `FSLanguage::validate()`, including dependent parsers and the no-frontend error. Update ScriptLanguageExtension legacy dictionary conversion to a one-character range and migrate every editor consumer to the range start.

- [ ] **Step 4: Rebuild and run range tests to confirm GREEN**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*diagnostic range*"
```

Expected: build succeeds and range tests pass.

- [ ] **Step 5: Write failing TextEdit decoration tests**

Add `TESTS_ENABLED` accessors for decoration count/data, then cover:

- add, clear, and color update;
- insertion before/start/inside/end;
- deletion before, partially overlapping either endpoint, and covering the whole range;
- multiline insertion/removal;
- `set_line()`, `insert_line_at()`, `remove_line_at()`, and `swap_lines()`;
- rejected/clamped invalid and reversed ranges.

Each subcase must assert exact zero-based start/end positions after the public TextEdit edit operation.

- [ ] **Step 6: Run TextEdit tests and confirm RED**

```sh
./bin/foundry.* --headless test run --case "*TextEdit*underline decoration*" --force-colors
```

Expected: FAIL because TextEdit has no decoration model.

- [ ] **Step 7: Implement robust TextEdit range transformation**

Adapt shaped-text squiggle drawing from `refs/upstream-pr/119588`, but centralize endpoint transformation for insertions and deletions. Map points inside a deleted span to the replacement start, shift points after the span by the line/column delta, apply start/end stickiness for insertion boundaries, normalize ordering, clamp to line lengths, and skip empty ranges. Correct the upstream middle-line split so the second piece begins at column `0`, not at the next line's length.

Ensure every mutation that calls `_offset_carets_after()` also transforms decorations, and preserve/split decorations correctly in `swap_lines()`.

- [ ] **Step 8: Run TextEdit tests and confirm GREEN**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*TextEdit*underline decoration*"
```

Expected: build succeeds and all edit-transformation cases pass.

- [ ] **Step 9: Integrate editor settings and rendering**

Add theme-derived `warning_underline_color` and `error_underline_color` settings and class-reference entries. Cache/update them in `ScriptTextEditor`, clear/rebuild decorations during `_update_background_color()`, and keep backgrounds independent from squiggle alpha. Normalize one-based diagnostic ranges before adding decorations.

- [ ] **Step 10: Run combined focused diagnostics tests**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*FoundryScript*diagnostic range*" \
  --case "*TextEdit*underline decoration*"
```

Expected: strict focused build and both test groups pass.

- [ ] **Step 11: Commit the isolated change**

```sh
git add core/object/script_language.h core/object/script_language_extension.h \
  doc/classes/EditorSettings.xml editor/script/script_text_editor.cpp \
  editor/script/script_text_editor.h editor/settings/editor_settings.cpp \
  editor/themes/editor_theme_manager.cpp modules/foundry_script/fs_editor.cpp \
  modules/foundry_script/fs_parser.cpp modules/foundry_script/fs_parser.h \
  modules/foundry_script/fs_warning.h scene/gui/text_edit.cpp scene/gui/text_edit.h \
  modules/foundry_script/tests/test_foundry_script.cpp tests/scene/test_text_edit.h
git commit -m "Underline Foundry Script diagnostics"
```

## Task 4: Integrate, Review, and Verify

**Files:**

- Modify only files needed to resolve integration or review findings.
- Create proof assets under ignored test scratch/review-gallery paths, not tracked fixture directories.

- [ ] **Step 1: Inspect combined history and working tree**

```sh
git status --short
git log --oneline --decorate -8
git diff develop...HEAD --check
```

Expected: only planned changes, no unrelated files, and no whitespace errors.

- [ ] **Step 2: Run independent specification and code-quality reviews**

Dispatch fresh review subagents with the design, plan, `develop` base SHA, and branch HEAD. Fix every Critical and Important finding, then rerun affected focused tests.

- [ ] **Step 3: Run Python tests**

```sh
python3 -m unittest scripts.tests.test_make_rst_admonitions -v
```

Expected: all pass.

- [ ] **Step 4: Run the native strict build and full suite**

```sh
python3 scripts/agent_build.py --test
```

Expected: build succeeds and the final doctest summary reports success. Record any cleanup-only nonzero exit separately according to repository guidance.

- [ ] **Step 5: Exercise the real editor through automation**

Launch an automation-enabled editor against a scratch project. Open a Foundry Script with a warning and parser error, open documentation containing all four callouts, inspect editor logs, and capture screenshots that prove both features render.

- [ ] **Step 6: Build a proof gallery**

```sh
python3 scripts/review_gallery.py board "Upstream editor improvements port" --mode proof
```

Add captioned screenshots showing callout labels/colors and warning/error squiggles. Serve locally, or publicly with basic auth only if the user asks for a remote URL.

- [ ] **Step 7: Final requirements audit**

Re-read the design and verify every goal/non-goal against the diff, test output, and visual evidence. Report exact commands, outcomes, commits, remaining limitations, and gallery location.
