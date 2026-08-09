# Editor Boards Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the editor's single tiling workspace into an ordered list of *boards*, each a whole tiling arrangement, with an animated switcher and a zoomed-out overview that panes can be dragged between and boards reordered in.

**Architecture:** A new `EditorBoardStrip` replaces the lone `EditorSceneWorkspace` in `EditorNode`, hosting N `EditorBoard` children laid edge-to-edge horizontally, each wrapping one unmodified `EditorSceneWorkspace`. Switching and the overview are a canvas transform (scroll offset + uniform scale) on the strip, not a viewport swap — which is what keeps cross-board drag working through ordinary `Control` drag-and-drop. Dormant boards are hidden and process-disabled, so steady-state cost is unchanged from today.

**Tech Stack:** C++ (Foundry engine fork), SCons build via `scripts/agent_build.py`, doctest via `tests/test_macros.h`, editor automation MCP for GUI acceptance, `scripts/review_gallery.py` for visual review.

**Spec:** `docs/superpowers/specs/2026-08-09-editor-boards-design.md`

---

## Conventions for every task

Read these once; they apply to all tasks and are not repeated.

**Build (fast iteration):**
```sh
python3 scripts/agent_build.py --backend ninja
```

**Build (required before any PR / handoff):**
```sh
python3 scripts/agent_build.py
```

**Run a scoped test suite:**
```sh
./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors
```

**Full suite (trust the `[doctest] Status: SUCCESS!` line, not the exit code — the
run reports leaked ObjectDB instances at cleanup even when green):**
```sh
DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors \
  --progress-format=jsonl --progress-file /tmp/foundry-boards-progress.jsonl
```

**New source files are picked up automatically** — `editor/SCsub:73` and
`editor/gui/SCsub:6` both glob `*.cpp`. No build-file edits are needed for any
new `.cpp` in `editor/` or `editor/gui/`.

**New test headers must be registered** by adding an `#include` to
`tests/test_main.cpp`, next to the existing `#include "tests/editor/..."` block
(see `tests/test_main.cpp:90` for the pattern).

**Style:** tabs, 120 columns, `snake_case` filenames, `FOUNDRY_CLASS` macro,
Foundry copyright header block copied verbatim from a sibling file such as
`editor/editor_scene_workspace.h`. No abbreviations in identifiers (`position`
not `pos`, `definition` not `def`). No comments referencing tasks, phases, or
this plan.

**Commit** at the end of every task. Do not batch tasks into one commit.

---

## File structure

| File | Responsibility |
| --- | --- |
| `editor/editor_board.{h,cpp}` | One board: owns a single `EditorSceneWorkspace`, its id/title/remembered focus, and `set_dormant()`. |
| `editor/editor_board_strip.{h,cpp}` | The board list, the global leaf-id allocator, horizontal layout, and view-transform application. Also hosts the overview caption overlay. |
| `editor/editor_board_view.{h,cpp}` | `Control`-free geometry + animation state: scroll offset, scale, index-at-point. Unit-testable headlessly. |
| `editor/gui/editor_board_switcher.{h,cpp}` | Title-bar chrome: per-board buttons, rename, `+`, overview toggle. Stateless. |
| `editor/editor_scene_workspace.{h,cpp}` | **Modified**: section-parameterised persistence; cross-board-tolerant `handle_tab_drop`; leaf ids supplied externally. |
| `editor/editor_node.{h,cpp}` | **Modified**: hosts the strip instead of one workspace; `get_scene_workspace()` redirects to the active board; save/load become board loops. |
| `editor/automation/editor_automation_workspace.{h,cpp}` | **Modified**: per-board state capture. |
| `tests/editor/test_editor_board_view.h` | Geometry unit tests. |
| `tests/editor/test_editor_board_strip.h` | Strip model: create/close/reorder, leaf-id uniqueness, last-board refusal. |
| `tests/editor/test_editor_board_persistence.h` | Multi-board round-trip, restore ordering, legacy-config rejection. |
| `tests/editor/test_editor_board_cross_board.h` | Cross-board tab drop and cross-board help refresh. |

---

## Stage 1 — Boards without motion

This stage carries all the invariant risk. At the end of it the editor has N
boards that persist correctly and switch instantly; there is no slide and no
overview.

### Task 1: Global leaf-id allocator

**Goal:** Move leaf-id allocation out of `EditorSceneWorkspace` so ids are unique across every workspace instance, with no behavior change while only one workspace exists.

**Why this is first:** `EditorData` keys scene ownership on tile id globally (`register_tile`, `get_tile_scene_indices`, `set_scene_tile`), and layout persistence keys sections on `WorkspaceLeaf_%d`. Two workspaces each starting at leaf 0 would silently share scene ownership and clobber each other's saved layout. Every later task depends on this holding.

**Files:**
- Modify: `editor/editor_scene_workspace.h` (private `next_leaf_id`, `create_single_leaf_workspace`, `split`, `split_with_content`)
- Modify: `editor/editor_scene_workspace.cpp:544`, `:569`, `:1364-1366`, `:1403`
- Test: `tests/editor/test_editor_board_strip.h` (new file, first test only)
- Modify: `tests/test_main.cpp`

**Acceptance Criteria:**
- [ ] `EditorSceneWorkspace` no longer contains a `next_leaf_id` member.
- [ ] `git grep -n "next_leaf_id" editor/editor_scene_workspace.cpp` returns zero matches.
- [ ] Two `EditorSceneWorkspace` instances sharing one allocator never produce the same leaf id.
- [ ] The existing `tests/editor/test_scene_workspace.h` suite passes unchanged.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --suite "*[Editor][SceneWorkspace]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Introduce the allocator interface**

Add to `editor/editor_scene_workspace.h`, above `class EditorSceneWorkspace`:

```cpp
/**
 * Supplies leaf ids to a workspace. Ids must be unique across every workspace in
 * the editor: EditorData keys scene-tile ownership on leaf id, and layout
 * persistence keys per-pane sections on it, so two workspaces issuing the same id
 * would share scene ownership and overwrite each other's saved layout.
 */
class WorkspaceLeafIdAllocator {
public:
	virtual int allocate_leaf_id() = 0;
	// Called during restore so a persisted id is never handed out again.
	virtual void reserve_leaf_id(int p_leaf_id) = 0;
	virtual ~WorkspaceLeafIdAllocator() {}
};

// Default allocator used when a workspace is constructed standalone (tests, and
// the transitional single-workspace path). Owns its own counter.
class LocalWorkspaceLeafIdAllocator : public WorkspaceLeafIdAllocator {
	int next_leaf_id = 0;

public:
	int allocate_leaf_id() override { return next_leaf_id++; }
	void reserve_leaf_id(int p_leaf_id) override { next_leaf_id = MAX(next_leaf_id, p_leaf_id + 1); }
	int peek_next_leaf_id() const { return next_leaf_id; }
	void set_next_leaf_id(int p_value) { next_leaf_id = p_value; }
};
```

- [ ] **Step 2: Write the failing test**

Create `tests/editor/test_editor_board_strip.h` with the standard Foundry header block, then:

```cpp
#pragma once

#include "editor/editor_scene_workspace.h"

#include "tests/test_macros.h"

namespace TestEditorBoardStrip {

TEST_CASE("[Editor][Boards] Shared allocator never repeats a leaf id across workspaces") {
	LocalWorkspaceLeafIdAllocator allocator;

	HashSet<int> seen;
	for (int i = 0; i < 16; i++) {
		const int id = allocator.allocate_leaf_id();
		CHECK_FALSE(seen.has(id));
		seen.insert(id);
	}

	// A restored id must never be handed out again.
	allocator.reserve_leaf_id(100);
	CHECK(allocator.allocate_leaf_id() == 101);
}

} // namespace TestEditorBoardStrip
```

Register it by adding `#include "tests/editor/test_editor_board_strip.h"` to `tests/test_main.cpp` in the alphabetised editor include block.

- [ ] **Step 3: Run the test and confirm it fails**

```sh
python3 scripts/agent_build.py --backend ninja
./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors
```
Expected: compile error — `LocalWorkspaceLeafIdAllocator` not declared. That is the red state.

- [ ] **Step 4: Thread the allocator through the workspace**

In `editor/editor_scene_workspace.h`, replace the `int next_leaf_id = 0;` member with:

```cpp
	WorkspaceLeafIdAllocator *leaf_id_allocator = nullptr;
	LocalWorkspaceLeafIdAllocator owned_leaf_id_allocator;
```

and add to the public section:

```cpp
	// When null, the workspace falls back to its own local allocator. EditorBoardStrip
	// injects the editor-wide allocator so ids never collide between boards.
	void set_leaf_id_allocator(WorkspaceLeafIdAllocator *p_allocator);
	WorkspaceLeafIdAllocator *get_leaf_id_allocator();
```

In `editor/editor_scene_workspace.cpp`, add:

```cpp
void EditorSceneWorkspace::set_leaf_id_allocator(WorkspaceLeafIdAllocator *p_allocator) {
	leaf_id_allocator = p_allocator;
}

WorkspaceLeafIdAllocator *EditorSceneWorkspace::get_leaf_id_allocator() {
	return leaf_id_allocator ? leaf_id_allocator : &owned_leaf_id_allocator;
}
```

- [ ] **Step 5: Replace every allocation site**

- `editor/editor_scene_workspace.cpp:544` — `workspace->_create_leaf(workspace->next_leaf_id++, ...)` becomes `workspace->_create_leaf(workspace->get_leaf_id_allocator()->allocate_leaf_id(), ...)`.
- `:569` — `_create_leaf(next_leaf_id++, p_content_type)` becomes `_create_leaf(get_leaf_id_allocator()->allocate_leaf_id(), p_content_type)`.
- `:1364-1366` — the restore fallback currently computes `next_leaf_id + leaves.size()`. Replace with a fresh `get_leaf_id_allocator()->allocate_leaf_id()`; the allocator already guarantees freshness, so the manual `MAX` bookkeeping goes away.
- `:1403` — `next_leaf_id = MAX(next_leaf_id, leaf_id + 1)` becomes `get_leaf_id_allocator()->reserve_leaf_id(leaf_id)`.

- [ ] **Step 6: Run the test and the existing workspace suite**

```sh
python3 scripts/agent_build.py --backend ninja
./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --suite "*[Editor][SceneWorkspace]*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`. The pre-existing workspace tests must pass with no edits — this task is behaviour-preserving.

- [ ] **Step 7: Commit**

```bash
git add editor/editor_scene_workspace.h editor/editor_scene_workspace.cpp \
        tests/editor/test_editor_board_strip.h tests/test_main.cpp
git commit -m "refactor(editor): make workspace leaf ids come from an injectable allocator"
```

---

### Task 2: Section-parameterised workspace persistence

**Goal:** Let a workspace save to and restore from an arbitrary config section, so several workspaces can coexist in one `editor_layout.cfg`. Behaviour-preserving: still writes `[Workspace]` when nothing passes a section name.

**Files:**
- Modify: `editor/editor_scene_workspace.h:239-243` (persistence declarations)
- Modify: `editor/editor_scene_workspace.cpp:1330-1440`
- Test: `tests/editor/test_editor_board_persistence.h` (new)
- Modify: `tests/test_main.cpp`

**Acceptance Criteria:**
- [ ] `save_to_config`, `has_workspace_session`, and `restore_from_config` each take a section name.
- [ ] Two workspaces saved to `Board_0` and `Board_1` in one `ConfigFile` restore independently and correctly.
- [ ] `git grep -n "WORKSPACE_CONFIG_SECTION" editor/` returns zero matches (the constant is gone, not merely unused).
- [ ] Per-leaf `[WorkspaceLeaf_N]` sections are untouched by this change.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test**

Create `tests/editor/test_editor_board_persistence.h` (standard header block), then:

```cpp
#pragma once

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"

#include "tests/test_macros.h"

namespace TestEditorBoardPersistence {

TEST_CASE("[Editor][Boards] Two workspaces persist to independent sections") {
	Ref<ConfigFile> config;
	config.instantiate();

	EditorData editor_data_a;
	EditorSelection *selection_a = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_a = EditorSceneWorkspace::create_single_leaf_workspace(selection_a, &editor_data_a);
	SceneTree::get_singleton()->get_root()->add_child(workspace_a);
	workspace_a->split(workspace_a->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);

	EditorData editor_data_b;
	EditorSelection *selection_b = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_b = EditorSceneWorkspace::create_single_leaf_workspace(selection_b, &editor_data_b);
	SceneTree::get_singleton()->get_root()->add_child(workspace_b);

	EditorSceneWorkspace::save_to_config(config, workspace_a, "Board_0");
	EditorSceneWorkspace::save_to_config(config, workspace_b, "Board_1");

	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_0"));
	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_1"));
	// Two leaves in A, one in B -- the sections must not have merged.
	CHECK(int(config->get_value("Board_0", "node_count")) == 3);
	CHECK(int(config->get_value("Board_1", "node_count")) == 1);

	memdelete(workspace_a);
	memdelete(workspace_b);
	memdelete(selection_a);
	memdelete(selection_b);
}

} // namespace TestEditorBoardPersistence
```

Register the include in `tests/test_main.cpp`.

- [ ] **Step 2: Run and confirm it fails**

```sh
python3 scripts/agent_build.py --backend ninja
```
Expected: compile error — `save_to_config` takes two arguments, not three.

- [ ] **Step 3: Change the signatures**

In `editor/editor_scene_workspace.h`, replace the persistence block:

```cpp
	// Persistence (nested tree, flat index-addressed node list). p_section names the
	// config section this workspace occupies; boards pass "Board_<n>" so several
	// workspaces coexist in one config. Per-leaf sections are keyed on the globally
	// unique leaf id and are therefore independent of p_section.
	static String leaf_layout_section(int p_leaf_id);
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorSceneWorkspace *p_workspace, const String &p_section);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config, const String &p_section);
	void restore_from_config(const Ref<ConfigFile> &p_config, const String &p_section);
```

Delete the private `WORKSPACE_CONFIG_SECTION` constant.

- [ ] **Step 4: Thread the section through the implementation**

In `editor/editor_scene_workspace.cpp`, replace every `WORKSPACE_CONFIG_SECTION` occurrence (lines 1334-1347, 1352-1359, 1371-1391, 1426-1438) with `p_section`. `_restore_node_from_config` gains a `const String &p_section` parameter and passes it down its recursion; update its declaration at `editor/editor_scene_workspace.h:137`.

Leave `leaf_layout_section()` exactly as it is — leaf ids are globally unique after Task 1, so per-leaf sections need no board qualifier.

- [ ] **Step 5: Update the two existing call sites**

`editor/editor_node.cpp:8200` and `:8212`/`:8229` pass the literal `"Workspace"` for now. Task 4 replaces this with the board loop.

- [ ] **Step 6: Run the suite**

```sh
python3 scripts/agent_build.py --backend ninja
./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --suite "*[Editor][SceneWorkspace]*" --force-colors
```
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 7: Commit**

```bash
git add editor/editor_scene_workspace.h editor/editor_scene_workspace.cpp editor/editor_node.cpp \
        tests/editor/test_editor_board_persistence.h tests/test_main.cpp
git commit -m "refactor(editor): parameterise workspace persistence by config section"
```

---

### Task 3: `EditorBoard` and `EditorBoardStrip` hosting exactly one board

**Goal:** Introduce both new classes and reroute `EditorNode` through them, with the strip holding exactly one board. The editor must look and behave identically to before.

**Why one board first:** this isolates the risky plumbing (construction order, focus wiring, projectless-shell hiding, `get_scene_workspace()` redirection) from the also-risky multi-board persistence in Task 4. If the editor regresses, exactly one of the two is at fault.

**Files:**
- Create: `editor/editor_board.h`, `editor/editor_board.cpp`
- Create: `editor/editor_board_strip.h`, `editor/editor_board_strip.cpp`
- Modify: `editor/editor_node.h:347`, `:882`
- Modify: `editor/editor_node.cpp:11040-11046`, `:7149`, `:11064-11070`
- Test: `tests/editor/test_editor_board_strip.h`

**Acceptance Criteria:**
- [ ] `EditorNode` holds an `EditorBoardStrip *board_strip` instead of `EditorSceneWorkspace *scene_workspace`.
- [ ] `EditorNode::get_scene_workspace()` returns the active board's workspace and keeps its exact signature and return type.
- [ ] `EditorNode::get_board_strip()` exists and is used by nothing yet except tests.
- [ ] The projectless shell hides the strip (`editor/editor_node.cpp:7149`), not a bare workspace.
- [ ] The full test suite is green with no test edits beyond the new file.
- [ ] Launching the editor GUI shows an editor indistinguishable from before the change.

**Verify:** `DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write `editor/editor_board.h`**

```cpp
#pragma once

#include "scene/gui/container.h"

class ConfigFile;
class EditorData;
class EditorSceneWorkspace;
class EditorSelection;

/**
 * One whole editing arrangement: a container owning exactly one
 * EditorSceneWorkspace plus the board's identity. Boards are siblings inside
 * EditorBoardStrip and are switched between rather than nested.
 */
class EditorBoard : public Container {
	FOUNDRY_CLASS(EditorBoard, Container);

	int board_id = 0;
	String title;
	// The leaf this board's workspace had focused when the user last left it.
	// Restored on activation so switching back lands where the user was.
	int focused_leaf_id = 0;
	bool dormant = false;
	EditorSceneWorkspace *workspace = nullptr;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorBoard *create(int p_board_id, const String &p_title, EditorSelection *p_editor_selection, EditorData *p_editor_data);

	int get_board_id() const { return board_id; }
	String get_title() const { return title; }
	void set_title(const String &p_title);

	EditorSceneWorkspace *get_workspace() const { return workspace; }

	int get_remembered_focused_leaf_id() const { return focused_leaf_id; }
	void remember_focused_leaf_id(int p_leaf_id) { focused_leaf_id = p_leaf_id; }

	// Dormancy is implemented as hide() + PROCESS_MODE_DISABLED. A hidden board's
	// SubViewportContainers force their children to UPDATE_DISABLED, so rendering
	// stops on its own. Do not gate SubViewport update modes directly instead:
	// nothing keys off on-screen position, so a board scrolled outside the viewport
	// rect while still visible would keep rendering its previews at full cost.
	void set_dormant(bool p_dormant);
	bool is_dormant() const { return dormant; }

	EditorBoard();
};
```

- [ ] **Step 2: Write `editor/editor_board.cpp`**

`create()` builds the board, calls `EditorSceneWorkspace::create_single_leaf_workspace(p_editor_selection, p_editor_data)`, adds it as the sole child with `set_v_size_flags(SIZE_EXPAND_FILL)`, and stores id/title.

`_notification(NOTIFICATION_SORT_CHILDREN)` calls `fit_child_in_rect(workspace, Rect2(Point2(), get_size()))`, matching the pattern in `WorkspaceLeafNode::_notification` (`editor/editor_scene_workspace.cpp:325`).

`set_dormant()`:

```cpp
void EditorBoard::set_dormant(bool p_dormant) {
	if (dormant == p_dormant) {
		return;
	}
	dormant = p_dormant;
	set_visible(!dormant);
	set_process_mode(dormant ? PROCESS_MODE_DISABLED : PROCESS_MODE_INHERIT);
}
```

`_bind_methods()` registers nothing yet; it exists so later tasks can add signals without churn.

- [ ] **Step 3: Write `editor/editor_board_strip.h`**

```cpp
#pragma once

#include "editor/editor_scene_workspace.h"

#include "scene/gui/container.h"

class EditorBoard;
class EditorData;
class EditorSelection;

/**
 * Ordered list of boards laid edge-to-edge horizontally, each sized to the full
 * strip rect. Owns the editor-wide leaf id allocator and is the only unit that
 * knows board geometry.
 */
class EditorBoardStrip : public Container, public WorkspaceLeafIdAllocator {
	FOUNDRY_CLASS(EditorBoardStrip, Container);

	Vector<EditorBoard *> boards;
	int active_index = 0;
	int next_board_id = 0;
	LocalWorkspaceLeafIdAllocator leaf_ids;

	EditorSelection *editor_selection = nullptr;
	EditorData *editor_data = nullptr;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorBoardStrip *create(EditorSelection *p_editor_selection, EditorData *p_editor_data);

	int allocate_leaf_id() override { return leaf_ids.allocate_leaf_id(); }
	void reserve_leaf_id(int p_leaf_id) override { leaf_ids.reserve_leaf_id(p_leaf_id); }
	int peek_next_leaf_id() const { return leaf_ids.peek_next_leaf_id(); }
	void set_next_leaf_id(int p_value) { leaf_ids.set_next_leaf_id(p_value); }

	int get_board_count() const { return boards.size(); }
	EditorBoard *get_board(int p_index) const;
	EditorBoard *get_active_board() const;
	int get_active_index() const { return active_index; }
	EditorSceneWorkspace *get_active_workspace() const;

	// Resolves a leaf id against every board, not just the active one. Cross-board
	// tab drops need this because a drag can start on one board and land on another.
	WorkspaceLeafNode *find_leaf_by_id(int p_leaf_id) const;
	EditorBoard *find_board_for_leaf(int p_leaf_id) const;

	// Runs p_callable once per board's workspace. Call sites that mean "every
	// workspace in the editor" (documentation refresh, restore-time layout probing)
	// use this instead of reaching through the active board.
	void for_each_workspace(const Callable &p_callable) const;

	EditorBoardStrip();
};
```

- [ ] **Step 4: Write `editor/editor_board_strip.cpp`**

`create()` instantiates the strip, stores selection/data, and appends one board via an internal `_append_board(title)` helper that allocates a `board_id`, calls `EditorBoard::create(...)`, injects the allocator with `board->get_workspace()->set_leaf_id_allocator(this)`, and `add_child`s it.

Important ordering: `set_leaf_id_allocator(this)` must run **before** anything creates a leaf. `create_single_leaf_workspace` creates the first leaf inside `EditorBoard::create`, so that leaf comes from the board's local allocator. Correct this by having `_append_board` call `reserve_leaf_id()` for the initial leaf's id immediately after injection — or, preferably, add an `EditorSceneWorkspace::create_single_leaf_workspace` overload taking the allocator. **Use the overload**; the reserve-after-the-fact version is fragile:

```cpp
static EditorSceneWorkspace *create_single_leaf_workspace(EditorSelection *p_editor_selection, EditorData *p_editor_data, WorkspaceLeafIdAllocator *p_allocator = nullptr);
```

`_notification(NOTIFICATION_SORT_CHILDREN)` positions board *i* at `Rect2(Point2(i * get_size().width, 0), get_size())`. Only the active board is non-dormant, so all others are hidden and cost nothing.

`find_leaf_by_id` loops boards calling `get_workspace()->get_leaf_by_id(p_leaf_id)` and returns the first hit.

`for_each_workspace` loops boards and calls `p_callable.call(board->get_workspace())`.

- [ ] **Step 5: Reroute `EditorNode`**

In `editor/editor_node.h`: change the member at `:347` to `EditorBoardStrip *board_strip = nullptr;` and replace the accessor at `:882`:

```cpp
	static EditorBoardStrip *get_board_strip() { return singleton ? singleton->board_strip : nullptr; }
	// The workspace the user is currently looking at. Call sites that mean "every
	// workspace" must use get_board_strip()->for_each_workspace() instead.
	static EditorSceneWorkspace *get_scene_workspace();
```

Move `get_scene_workspace()` into the `.cpp` as `return board_strip ? board_strip->get_active_workspace() : nullptr;` (it can no longer be a header one-liner, since it now dereferences an incomplete type).

At `editor/editor_node.cpp:11040-11046`, replace the workspace construction with strip construction, then take `scene_workspace` as a local for the remaining wiring:

```cpp
	board_strip = EditorBoardStrip::create(editor_selection, &editor_data);
	srt->add_child(board_strip);
	board_strip->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	EditorSceneWorkspace *scene_workspace = board_strip->get_active_workspace();
	scene_workspace->connect("leaf_focus_requested", callable_mp(this, &EditorNode::_on_leaf_focus_requested));
	scene_workspace->connect("leaf_added", callable_mp(this, &EditorNode::_on_leaf_added));
	scene_workspace->connect("leaf_about_to_remove", callable_mp(this, &EditorNode::_on_leaf_about_to_remove));
	scene_workspace->connect("leaf_removed", callable_mp(this, &EditorNode::_on_leaf_removed));
```

Extract those four `connect` calls into a private `EditorNode::_connect_workspace_signals(EditorSceneWorkspace *)`; Task 5 calls it for every newly created board.

At `:7149`, change `scene_workspace->hide()` to `board_strip->hide()`.

Every other `scene_workspace` reference in `editor_node.cpp` becomes `get_scene_workspace()`. There are roughly 40; they are all active-board semantics and need no further thought at this stage. Task 6 handles the ones outside `editor_node.cpp`.

- [ ] **Step 6: Add the strip test**

Append to `tests/editor/test_editor_board_strip.h`:

```cpp
TEST_CASE("[Editor][Boards] A fresh strip has exactly one active board") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	CHECK(strip->get_board_count() == 1);
	CHECK(strip->get_active_index() == 0);
	CHECK(strip->get_active_workspace() != nullptr);
	CHECK(strip->get_board(0)->is_dormant() == false);

	// The strip resolves its own board's leaves.
	WorkspaceLeafNode *leaf = strip->get_active_workspace()->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	CHECK(strip->find_leaf_by_id(leaf->get_leaf_id()) == leaf);
	CHECK(strip->find_board_for_leaf(leaf->get_leaf_id()) == strip->get_board(0));

	memdelete(strip);
	memdelete(selection);
}
```

- [ ] **Step 7: Build, run the full suite, and smoke the GUI**

```sh
python3 scripts/agent_build.py
DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors
DISPLAY=:1 ./bin/foundry.* editor open --project <a test project>
```
Expected: `[doctest] Status: SUCCESS!`, and a GUI editor indistinguishable from before.

- [ ] **Step 8: Commit**

```bash
git add editor/editor_board.h editor/editor_board.cpp \
        editor/editor_board_strip.h editor/editor_board_strip.cpp \
        editor/editor_node.h editor/editor_node.cpp editor/editor_scene_workspace.h \
        editor/editor_scene_workspace.cpp tests/editor/test_editor_board_strip.h
git commit -m "feat(editor): host the scene workspace inside a board strip"
```

---

### Task 4: Multi-board persistence

**Goal:** Save and restore N boards, with the global restore steps correctly hoisted out of the per-board loop.

**The hazard:** `EditorNode::_load_workspace_from_config` (`editor/editor_node.cpp:8211-8260`) does several things that are global, not per-workspace. `restore_from_config()` **frees the outgoing workspace tree**, so the shared scene-mode surface must be detached before any board rebuild and reattached once after. Running that per board either detaches/reattaches repeatedly or, worse, leaves the surface parented to a freed tile host. This is the most likely source of a use-after-free in the whole feature.

**Files:**
- Modify: `editor/editor_board_strip.{h,cpp}` (save/restore)
- Modify: `editor/editor_node.cpp:8198-8203`, `:8211-8265`
- Test: `tests/editor/test_editor_board_persistence.h`

**Acceptance Criteria:**
- [ ] Three boards with distinct split trees round-trip to identical trees, tabs, and per-board focus.
- [ ] `next_leaf_id` is persisted and restored; a board added after restore never reuses a restored leaf id.
- [ ] A config with a legacy `[Workspace]` section and no `[Boards]` section restores as a single default board, adopting nothing from the stale section.
- [ ] `active_board` out of range clamps to the nearest valid index rather than crashing.
- [ ] The scene-mode surface is detached exactly once before any board rebuild and reattached exactly once after.
- [ ] No migration code is added. `git grep -in "migrat" editor/editor_board_strip.cpp editor/editor_node.cpp` returns no matches introduced by this task.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing round-trip test**

Append to `tests/editor/test_editor_board_persistence.h`:

```cpp
TEST_CASE("[Editor][Boards] Three boards round-trip independently") {
	Ref<ConfigFile> config;
	config.instantiate();

	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	strip->add_board();
	strip->add_board();
	strip->get_board(1)->set_title("face shader");
	// Give board 1 a split so the boards are structurally distinguishable.
	EditorSceneWorkspace *workspace_1 = strip->get_board(1)->get_workspace();
	workspace_1->split(workspace_1->get_focused_leaf(), true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	strip->set_active_board(1);

	const int leaf_high_water = strip->peek_next_leaf_id();
	EditorBoardStrip::save_to_config(config, strip);

	CHECK(int(config->get_value("Boards", "board_count")) == 3);
	CHECK(int(config->get_value("Boards", "active_board")) == 1);
	CHECK(String(config->get_value("Boards", "board_1_title")) == "face shader");
	CHECK(int(config->get_value("Boards", "next_leaf_id")) == leaf_high_water);
	CHECK(int(config->get_value("Board_0", "node_count")) == 1);
	CHECK(int(config->get_value("Board_1", "node_count")) == 3);

	memdelete(strip);
	memdelete(selection);
}

TEST_CASE("[Editor][Boards] A legacy Workspace section is ignored, not adopted") {
	Ref<ConfigFile> config;
	config.instantiate();
	// A pre-boards layout: one workspace with a split, and no [Boards] section.
	config->set_value("Workspace", "node_count", 3);
	config->set_value("Workspace", "root_node", 0);
	config->set_value("Workspace", "focused_leaf_id", 0);

	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	CHECK_FALSE(EditorBoardStrip::has_board_session(config));
	// Restoring is a no-op; the strip keeps its single default board.
	CHECK(strip->get_board_count() == 1);
	CHECK(strip->get_active_workspace()->get_leaf_count() == 1);

	memdelete(strip);
	memdelete(selection);
}

TEST_CASE("[Editor][Boards] An out-of-range active_board clamps") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 2);
	config->set_value("Boards", "active_board", 7);
	config->set_value("Boards", "board_0_id", 0);
	config->set_value("Boards", "board_1_id", 1);
	config->set_value("Board_0", "node_count", 1);
	config->set_value("Board_0", "root_node", 0);
	config->set_value("Board_1", "node_count", 1);
	config->set_value("Board_1", "root_node", 0);

	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);
	strip->restore_from_config(config);

	CHECK(strip->get_board_count() == 2);
	CHECK(strip->get_active_index() == 1);

	memdelete(strip);
	memdelete(selection);
}
```

- [ ] **Step 2: Run and confirm it fails**

Expected: compile errors — `add_board`, `set_active_board`, `save_to_config`, `has_board_session`, `restore_from_config` do not exist on the strip. `add_board`/`set_active_board` are added properly in Task 5; add minimal versions here (append a board; set `active_index` and toggle dormancy) and let Task 5 layer the focus and signal behaviour on top.

- [ ] **Step 3: Add the strip persistence API**

```cpp
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorBoardStrip *p_strip);
	static bool has_board_session(const Ref<ConfigFile> &p_config);
	void restore_from_config(const Ref<ConfigFile> &p_config);
```

`save_to_config` erases and rewrites `[Boards]`, writes `board_count`, `active_board`, `next_leaf_id`, and `board_<i>_id` / `board_<i>_title` / `board_<i>_focused_leaf` per board, then calls `EditorSceneWorkspace::save_to_config(p_config, board->get_workspace(), vformat("Board_%d", i))` for each.

`has_board_session` returns true only when `[Boards]` exists **and** carries `board_count >= 1`. It must not consult `[Workspace]`.

`restore_from_config` clears the board list, rebuilds `board_count` boards from `board_<i>_*`, calls `set_next_leaf_id()` from the persisted value **before** any board is built so restored ids are never reissued, then calls each workspace's `restore_from_config(p_config, vformat("Board_%d", i))`, and finally clamps `active_board` into `[0, board_count - 1]`.

- [ ] **Step 4: Rewrite the `EditorNode` save/load as a hoisted loop**

Replace `EditorNode::_save_workspace_to_config` (`:8198`) body with `EditorBoardStrip::save_to_config(p_config_file, board_strip);`.

Rewrite `_load_workspace_from_config` (`:8211`) with this exact nesting:

```
if (!board_strip || !EditorBoardStrip::has_board_session(config)) return false;

// ---- global, once, BEFORE any board rebuild ----
detach_remote_scene_tree()
detach the scene-mode control from its parent      // editor_node.cpp:8221-8228

board_strip->restore_from_config(config);          // rebuilds every board

// ---- per board, inside the loop (inside restore_from_config or driven here) ----
for each board:
    _connect_workspace_signals(board->get_workspace())
    for each leaf: _on_leaf_added(leaf_id)
    board->get_workspace()->restore_scene_tile_ownership_from_tabs()
    board->get_workspace()->resolve_script_leaf_associated_scenes(editor_data)
    board->get_workspace()->reconcile_empty_leaves()

// ---- global, once, AFTER every board rebuild ----
resolve the active board's focused leaf, falling back to its first scene tile
editor_data.set_focused_tile_id(active focused leaf id)
_bind_all_leaf_docks()
rebind_remote_scene_tree()
_sync_focused_tile_chrome(active focused tile)
```

The script-leaf focus fallback (`editor_node.cpp:8240-8250`) applies **only to the active board**; dormant boards keep whatever focus they persisted, since they own no scene-mode surface.

- [ ] **Step 5: Add the restore-ordering regression test**

The ordering must be observable without an `EditorNode`, so `restore_from_config`
emits `boards_about_to_restore` before it frees anything and `boards_restored`
after every board is rebuilt. `EditorNode` hangs its detach on the first and its
reattach on the second, which makes "exactly once, around the whole loop"
structurally true rather than a convention someone can quietly break.

```cpp
TEST_CASE("[Editor][Boards] Restore brackets every board rebuild exactly once") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 3);
	config->set_value("Boards", "active_board", 0);
	for (int i = 0; i < 3; i++) {
		config->set_value("Boards", vformat("board_%d_id", i), i);
		config->set_value(vformat("Board_%d", i), "node_count", 1);
		config->set_value(vformat("Board_%d", i), "root_node", 0);
	}

	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	SIGNAL_WATCH(strip, "boards_about_to_restore");
	SIGNAL_WATCH(strip, "boards_restored");

	strip->restore_from_config(config);

	// Exactly one bracket for three rebuilt boards. A per-board detach would fire
	// these three times and leave the shared scene-mode surface parented to a freed
	// tile host on every board after the first.
	SIGNAL_CHECK("boards_about_to_restore", { {} });
	SIGNAL_CHECK("boards_restored", { {} });
	CHECK(strip->get_board_count() == 3);

	SIGNAL_UNWATCH(strip, "boards_about_to_restore");
	SIGNAL_UNWATCH(strip, "boards_restored");
	memdelete(strip);
	memdelete(selection);
}
```

- [ ] **Step 6: Run the suite and commit**

```sh
python3 scripts/agent_build.py
DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors
git add editor/editor_board_strip.h editor/editor_board_strip.cpp editor/editor_node.cpp \
        tests/editor/test_editor_board_persistence.h
git commit -m "feat(editor): persist and restore multiple boards"
```

---

### Task 5: Board lifecycle — add, close, activate

**Goal:** Create, close, and switch boards, with per-scene unsaved-changes prompts on close and last-board refusal.

**Files:**
- Modify: `editor/editor_board_strip.{h,cpp}`
- Modify: `editor/editor_node.cpp` (close routing, `_connect_workspace_signals` for new boards)
- Test: `tests/editor/test_editor_board_strip.h`

**Acceptance Criteria:**
- [ ] `add_board()` returns a board whose workspace has one leaf, registered with `EditorData::register_tile`.
- [ ] `set_active_board(i)` wakes the incoming board before sleeping the outgoing one, and restores that board's remembered focused leaf.
- [ ] `close_board(i)` routes every scene in the board through the existing scene-close path, so unsaved-changes prompts fire per scene.
- [ ] Cancelling any prompt aborts the whole close; the board and all its scenes survive intact.
- [ ] `close_board()` on the only remaining board is refused and returns false.
- [ ] Signals `board_added`, `board_removed`, `active_board_changed` fire exactly once per operation.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("[Editor][Boards] Boards can be added, activated, and closed") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	SIGNAL_WATCH(strip, "board_added");
	SIGNAL_WATCH(strip, "active_board_changed");

	EditorBoard *second = strip->add_board();
	REQUIRE(second != nullptr);
	CHECK(strip->get_board_count() == 2);
	CHECK(second->get_workspace()->get_leaf_count() == 1);
	SIGNAL_CHECK("board_added", { { 1 } });

	// Leaf ids never collide between boards.
	const int leaf_a = strip->get_board(0)->get_workspace()->get_focused_leaf()->get_leaf_id();
	const int leaf_b = second->get_workspace()->get_focused_leaf()->get_leaf_id();
	CHECK(leaf_a != leaf_b);

	strip->set_active_board(1);
	CHECK(strip->get_active_index() == 1);
	CHECK_FALSE(strip->get_board(1)->is_dormant());
	CHECK(strip->get_board(0)->is_dormant());
	SIGNAL_CHECK("active_board_changed", { { 1 } });

	SIGNAL_UNWATCH(strip, "board_added");
	SIGNAL_UNWATCH(strip, "active_board_changed");
	memdelete(strip);
	memdelete(selection);
}

TEST_CASE("[Editor][Boards] The last board cannot be closed") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	CHECK(strip->get_board_count() == 1);
	CHECK_FALSE(strip->close_board(0));
	CHECK(strip->get_board_count() == 1);

	memdelete(strip);
	memdelete(selection);
}

TEST_CASE("[Editor][Boards] Activating a board restores its remembered focus") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	EditorBoard *second = strip->add_board();
	EditorSceneWorkspace *workspace = second->get_workspace();
	WorkspaceLeafNode *split_leaf = workspace->split(workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	strip->set_active_board(1);
	workspace->set_focused_leaf(split_leaf->get_leaf_id());

	strip->set_active_board(0);
	strip->set_active_board(1);
	CHECK(workspace->get_focused_leaf_id() == split_leaf->get_leaf_id());

	memdelete(strip);
	memdelete(selection);
}
```

- [ ] **Step 2: Run and confirm failure**

Expected: compile errors for `close_board` and the three signals.

- [ ] **Step 3: Implement the lifecycle**

```cpp
	EditorBoard *add_board(const String &p_title = String());
	// Returns false when the close is refused (last board) or aborted (a scene's
	// unsaved-changes prompt was cancelled).
	bool close_board(int p_index);
	void set_active_board(int p_index);
```

`add_board` allocates the next `board_id`, defaults the title to `vformat(TTR("Board %d"), boards.size() + 1)`, builds the board with the strip as leaf-id allocator, `add_child`s it, sets it dormant, emits `board_added`, and lets `EditorNode` register the initial tile by connecting to `board_added` — mirroring `editor/editor_node.cpp:11064-11070`.

`set_active_board` in this task is instant (no tween):

```cpp
void EditorBoardStrip::set_active_board(int p_index) {
	ERR_FAIL_INDEX(p_index, boards.size());
	if (p_index == active_index) {
		return;
	}
	EditorBoard *outgoing = boards[active_index];
	if (outgoing) {
		outgoing->remember_focused_leaf_id(outgoing->get_workspace()->get_focused_leaf_id());
	}
	// Wake before sleeping so there is never a frame with no live board.
	boards[p_index]->set_dormant(false);
	if (outgoing) {
		outgoing->set_dormant(true);
	}
	active_index = p_index;
	queue_sort();
	boards[p_index]->get_workspace()->request_leaf_focus(boards[p_index]->get_remembered_focused_leaf_id());
	emit_signal("active_board_changed", active_index);
}
```

`close_board` refuses when `boards.size() <= 1`. Otherwise it collects the board's scene indices via `editor_data->get_tile_scene_indices()` for every leaf, routes each through the existing `EditorNode` scene-close path, and aborts the whole operation if any prompt is cancelled. Because prompting is asynchronous in the GUI, expose the refusal/abort decision through a callback the `EditorNode` supplies; the headless test drives the no-scenes path where no prompt is needed.

- [ ] **Step 4: Wire `EditorNode` to `board_added`**

Connect `board_added` to a private `EditorNode::_on_board_added(int p_index)` that calls `_connect_workspace_signals()`, `editor_data.register_tile()` for the board's initial leaf, and `_wire_leaf_tile()`.

- [ ] **Step 5: Run and commit**

```sh
python3 scripts/agent_build.py
DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors
git add editor/editor_board_strip.h editor/editor_board_strip.cpp editor/editor_node.h editor/editor_node.cpp \
        tests/editor/test_editor_board_strip.h
git commit -m "feat(editor): add, activate, and close boards"
```

---

### Task 6: Reclassify the cross-board call sites

**Goal:** Fix the three `get_scene_workspace()` call sites that mean "every workspace in the editor" and would otherwise silently operate on the active board only.

**Why this matters:** these are invisible bugs. A class-reference tab open on a dormant board would show stale documentation forever, with nothing to indicate why.

**Files:**
- Modify: `editor/script/script_editor_controller.cpp:871`
- Modify: `editor/file_system/editor_file_system.cpp:2587`
- Modify: `editor/script/script_editor_plugin.cpp:670-671`
- Test: `tests/editor/test_editor_board_cross_board.h` (new)
- Modify: `tests/test_main.cpp`

**Acceptance Criteria:**
- [ ] A class-reference tab open on a **dormant** board refreshes when that class's documentation changes.
- [ ] The restore-time per-leaf script layout probe in `script_editor_plugin.cpp` sees script leaves on every board.
- [ ] The remaining active-board call sites are left unchanged, and the plan's classification is recorded as a comment at each all-boards site explaining why it is not `get_scene_workspace()`.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test**

Create `tests/editor/test_editor_board_cross_board.h`:

```cpp
TEST_CASE("[Editor][Boards] Documentation refresh reaches a dormant board") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorBoardStrip *strip = EditorBoardStrip::create(selection, &editor_data);
	SceneTree::get_singleton()->get_root()->add_child(strip);

	EditorBoard *second = strip->add_board();
	// Open a class-reference tab on board 1, then switch away so it goes dormant.
	EditorSceneWorkspace *dormant_workspace = second->get_workspace();
	WorkspaceLeafNode *help_leaf = dormant_workspace->open_help_tab(dormant_workspace->get_focused_leaf(), "Node2D");
	REQUIRE(help_leaf != nullptr);
	CHECK(strip->get_active_index() == 0);
	CHECK(second->is_dormant());

	// The routing, not the repaint, is what regresses: assert that iterating
	// workspaces reaches the dormant board, and that the active board alone does not.
	Vector<EditorSceneWorkspace *> visited;
	strip->for_each_workspace(callable_mp_static(&collect_workspace).bind(&visited));

	CHECK(visited.size() == 2);
	CHECK(visited.has(dormant_workspace));
	CHECK(visited.has(strip->get_active_workspace()));
	// The dormant board really does own the help tab that must be refreshed.
	CHECK(dormant_workspace->find_leaf_by_help_class("Node2D") == help_leaf);

	memdelete(strip);
	memdelete(selection);
}
```

`collect_workspace` is a file-local free function appending its argument to the
passed vector. `find_leaf_by_help_class` does not exist yet — add it alongside
`find_script_leaf_for_path` (`editor/editor_scene_workspace.h:164`), which
already does exactly this shape of lookup for scripts. Assert on that public
lookup, never on source text or on a private field reached by cast.

- [ ] **Step 2: Fix `script_editor_controller.cpp:866-873`**

```cpp
	if (p_script.is_valid()) {
		// Every board, not just the active one: a class-reference page open on a
		// dormant board would otherwise keep showing stale documentation with no
		// visible cause.
		if (EditorBoardStrip *strip = EditorNode::get_board_strip()) {
			for (const DocData::ClassDoc &cd : p_script->get_documentation()) {
				strip->for_each_workspace(/* refresh cd.qualified_name() */);
			}
		}
	}
```

- [ ] **Step 3: Fix `editor_file_system.cpp:2582-2590`** the same way, with the same rationale comment.

- [ ] **Step 4: Fix `script_editor_plugin.cpp:670-671`**

The legacy-layout probe must iterate script leaves across every board, otherwise a multi-board layout is misclassified as legacy and the wrong restore path runs. Replace the single-workspace loop with a `for_each_workspace` pass that ORs `has_per_leaf_layout` across boards.

- [ ] **Step 5: Record the classification**

Add a short comment at `EditorNode::get_scene_workspace()`'s definition stating that it means *the active board* and that call sites meaning "every workspace" must use `get_board_strip()->for_each_workspace()`.

- [ ] **Step 6: Run and commit**

```sh
python3 scripts/agent_build.py
DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors
git add editor/script/script_editor_controller.cpp editor/file_system/editor_file_system.cpp \
        editor/script/script_editor_plugin.cpp editor/editor_node.cpp \
        tests/editor/test_editor_board_cross_board.h tests/test_main.cpp
git commit -m "fix(editor): route documentation refresh and layout probing across all boards"
```

---

### Task 7: Title-bar board switcher

**Goal:** A visible switcher so boards are reachable without an API, plus rebindable shortcuts.

**Files:**
- Create: `editor/gui/editor_board_switcher.h`, `editor/gui/editor_board_switcher.cpp`
- Modify: `editor/editor_node.cpp:11311` (insert after `project_run_bar`)

**Acceptance Criteria:**
- [ ] One button per board, labelled with the board title, with the active board visually distinguished.
- [ ] Double-clicking a board button renames it inline; the new title persists across a save/restore cycle.
- [ ] A `+` button adds a board and activates it.
- [ ] `editor/previous_board` and `editor/next_board` are registered via `ED_SHORTCUT_AND_COMMAND` with defaults `CMD_OR_CTRL | ALT | LEFT` and `CMD_OR_CTRL | ALT | RIGHT`, and appear in the command palette.
- [ ] `git grep -n "previous_board\|next_board" editor/` shows both registered exactly once.
- [ ] The switcher rebuilds itself from `board_added` / `board_removed` / `board_moved` / `active_board_changed` and holds no board state of its own.

**Verify:** GUI smoke — launch the editor, add two boards, rename one, switch with the keyboard, restart, and confirm all three boards and the rename survived.

**Steps:**

- [ ] **Step 1: Confirm the shortcut defaults are free**

```sh
git grep -n "CMD_OR_CTRL | KeyModifierMask::ALT | Key::LEFT" editor/
git grep -n "CMD_OR_CTRL | KeyModifierMask::ALT | Key::RIGHT" editor/
```
Expected: zero matches. If a match appears (a sibling PR may have landed one), pick another free combination and note it in the PR body rather than silently double-binding.

- [ ] **Step 2: Write the switcher**

A `HBoxContainer` holding one `Button` per board plus a `+` `Button` and an overview toggle `Button` (the toggle is inert until Task 10). It connects to the strip's four signals and fully rebuilds its children on each; with a handful of boards this is cheaper and far less bug-prone than incremental updates. Inline rename uses a `LineEdit` swapped in over the button on double-click, committing on `text_submitted` and on focus loss.

- [ ] **Step 3: Mount it**

In `editor/editor_node.cpp`, immediately after `title_bar->add_child(project_run_bar);` (`:11311`):

```cpp
	board_switcher = memnew(EditorBoardSwitcher);
	board_switcher->setup(board_strip);
	title_bar->add_child(board_switcher);
```

- [ ] **Step 4: Register the shortcuts**

```cpp
	ED_SHORTCUT_AND_COMMAND("editor/previous_board", TTRC("Previous Board"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::ALT | Key::LEFT);
	ED_SHORTCUT_AND_COMMAND("editor/next_board", TTRC("Next Board"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::ALT | Key::RIGHT);
```

- [ ] **Step 5: GUI smoke and commit**

```sh
python3 scripts/agent_build.py
DISPLAY=:1 ./bin/foundry.* editor open --project <a test project>
git add editor/gui/editor_board_switcher.h editor/gui/editor_board_switcher.cpp editor/editor_node.h editor/editor_node.cpp
git commit -m "feat(editor): add the title-bar board switcher"
```

**Stage 1 is complete here.** The editor has N persistent boards with instant switching. Run the full suite and the strict build before moving on.

---

## Stage 2 — Motion

### Task 8: `EditorBoardView` geometry

**Goal:** The `Control`-free geometry and animation state, fully unit-tested headlessly.

**Files:**
- Create: `editor/editor_board_view.h`, `editor/editor_board_view.cpp`
- Test: `tests/editor/test_editor_board_view.h` (new)
- Modify: `tests/test_main.cpp`

**Acceptance Criteria:**
- [ ] `scroll_offset_for_index()` places board *n* exactly at the viewport origin at scale 1.
- [ ] `overview_scale_for()` fits N boards plus inter-board gutters inside the viewport width, and never exceeds 1.
- [ ] `index_at_point()` returns the board under a point in overview, and -1 outside every board.
- [ ] Every function is `static` or `const` and touches no `Control`; the test file includes no scene headers.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][BoardView]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("[Editor][BoardView] Scroll offset lands each board at the origin") {
	const Size2 viewport(1600, 900);
	CHECK(EditorBoardView::scroll_offset_for_index(0, viewport) == doctest::Approx(0.0));
	CHECK(EditorBoardView::scroll_offset_for_index(1, viewport) == doctest::Approx(-1600.0));
	CHECK(EditorBoardView::scroll_offset_for_index(3, viewport) == doctest::Approx(-4800.0));
}

TEST_CASE("[Editor][BoardView] Overview scale fits every board") {
	const Size2 viewport(1600, 900);
	// One board must not be blown up past its natural size.
	CHECK(EditorBoardView::overview_scale_for(1, viewport) == doctest::Approx(1.0));
	// Three boards plus gutters must fit inside the viewport width.
	const real_t scale = EditorBoardView::overview_scale_for(3, viewport);
	CHECK(scale < 1.0);
	CHECK(3 * viewport.width * scale + 2 * EditorBoardView::OVERVIEW_GUTTER <= viewport.width);
}

TEST_CASE("[Editor][BoardView] Point hit-testing selects the right board") {
	const Size2 viewport(1600, 900);
	EditorBoardView view;
	view.enter_overview(3, 1, viewport);

	// The centred active board contains the viewport centre.
	CHECK(view.index_at_point(Point2(800, 450), 3, viewport) == 1);
	// Far above every board is empty space.
	CHECK(view.index_at_point(Point2(800, 5), 3, viewport) == -1);
}
```

- [ ] **Step 2: Implement `EditorBoardView`**

State: `real_t scroll_x`, `real_t scale`, `int active_index`, plus target values and a normalised `transition` in `[0, 1]` advanced by `advance(delta)` with an ease-out curve. It exposes `get_transform()` returning the `Transform2D` the strip applies, and `is_animating()`.

`OVERVIEW_GUTTER` is a `static constexpr real_t` on the class so the test and the implementation cannot drift.

- [ ] **Step 3: Run and commit**

```sh
python3 scripts/agent_build.py --backend ninja
./bin/foundry.* --headless test run --suite "*[Editor][BoardView]*" --force-colors
git add editor/editor_board_view.h editor/editor_board_view.cpp tests/editor/test_editor_board_view.h tests/test_main.cpp
git commit -m "feat(editor): add board view geometry"
```

---

### Task 9: Animated board switching

**Goal:** Replace the instant switch with the horizontal slide, with both boards live during the transition.

**Files:**
- Modify: `editor/editor_board_strip.{h,cpp}`
- Test: `tests/editor/test_editor_board_strip.h`

**Acceptance Criteria:**
- [ ] Both the outgoing and incoming boards are non-dormant for the duration of the slide, and every other board stays dormant.
- [ ] The outgoing board goes dormant only after the animation completes.
- [ ] Interrupting a slide with another switch retargets from the current position rather than snapping.
- [ ] `active_board_changed` fires once at the start of the switch, not once per animation frame.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`, plus a GUI smoke of the slide.

**Steps:**

- [ ] **Step 1: Write the failing test** asserting the dormancy window — after `set_active_board(1)` both boards 0 and 1 are awake and board 2 is dormant; after advancing the view past completion, board 0 is dormant.

- [ ] **Step 2: Drive the view from `_process`**

`set_process(true)` while `view.is_animating()`; each frame call `view.advance(delta)`, apply `view.get_transform()` to the strip's child positions via `queue_sort()`, and on completion sleep the outgoing board and `set_process(false)`.

- [ ] **Step 3: GUI smoke, run the suite, commit.**

---

### Task 10: Overview mode

**Goal:** Zoom every board out into a live filmstrip with crisp captions, bounded in both resolution and refresh cadence.

**Files:**
- Modify: `editor/editor_board_strip.{h,cpp}` (overview state, caption overlay)
- Modify: `editor/editor_scene_pane_tile.{h,cpp}` (expose preview viewport resizing)
- Modify: `editor/gui/editor_board_switcher.cpp` (activate the toggle)

**Acceptance Criteria:**
- [ ] Every board is awake and live in overview; no board shows a frozen image.
- [ ] Entering overview lowers each preview container's render resolution via `set_stretch_shrink()` and exiting restores it exactly — verifiable by reading the resulting `SubViewport` size before, during, and after, and by asserting that no `Can't change the size of a SubViewport` warning is emitted.
- [ ] Overview previews refresh on a throttled tick rather than every frame.
- [ ] Captions render unscaled at full font size and are clickable.
- [ ] While overview is active, clicking inside a board selects that board and does not retarget the main screen.
- [ ] Exiting overview leaves exactly one non-dormant board.

**Verify:** GUI smoke plus `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors`.

**Steps:**

- [ ] **Step 1: Add a preview render-resolution seam to `ScenePaneTile`**

`preview_3d_viewport` is private with no setter (`editor/editor_scene_pane_tile.h:98`). Add:

```cpp
	// Overview mode draws tiles at a fraction of their layout size; matching the
	// preview render resolution to that on-screen size is what keeps a live
	// filmstrip affordable. Passing 1 restores full resolution.
	void set_preview_render_shrink(int p_shrink);
```

Implement it with `SubViewportContainer::set_stretch_shrink()`, **not**
`SubViewport::set_size()`. The preview container enables stretch
(`editor/editor_scene_pane_tile.cpp:273-282`), and `SubViewport::_internal_set_size`
returns early with a `WARN_PRINT` when the parent container has stretch enabled
(`scene/main/viewport.cpp:5442-5450`) — so `set_size` would silently no-op and
leave every board rendering at full resolution, removing the bound that makes the
overview affordable. `set_size_2d_override` is not an alternative: it changes only
the 2D coordinate space and does not reduce 3D render cost.

- [ ] **Step 2: Implement overview on the strip**

`set_overview(bool)` wakes every board, calls `set_preview_render_shrink()` on every tile of every board, tweens `EditorBoardView` to the overview scale and centring offset, and shows the caption overlay. Exiting reverses all four.

- [ ] **Step 3: Suppress focus retargeting**

While `is_overview_active()`, the strip intercepts `leaf_focus_requested` from every board and converts it into a board selection.

- [ ] **Step 4: Caption overlay**

A sibling `Control` of the boards, drawn after them, positioned from `EditorBoardView` geometry and **not** part of the scaled transform.

- [ ] **Step 5: GUI smoke, suite, commit.**

---

### Task 11: Cross-board drag

**Goal:** Drag a pane out of one board and drop it into another.

**Files:**
- Modify: `editor/editor_scene_workspace.cpp:143-214` (`handle_tab_drop`, `handle_tab_strip_drop`)
- Test: `tests/editor/test_editor_board_cross_board.h`

**Acceptance Criteria:**
- [ ] A scene tab dragged from board A into board B transfers `EditorData` tile ownership, appears in B, and disappears from A.
- [ ] A script tab does the same through `take_tab`/`add_tab`.
- [ ] Both the source and destination workspaces are re-synced after a cross-board move.
- [ ] An emptied source pane collapses, and the source board's remaining layout is intact.
- [ ] Same-board drops behave exactly as before — the existing drop tests pass unchanged.
- [ ] A drop with an unresolvable source pane returns `nullptr` and performs no partial move.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --suite "*[Editor][SceneWorkspace]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test** — build a two-board strip, open a scene on board 0, call `handle_tab_drop` on board 1's workspace with board 0's pane id, and assert ownership moved, both workspaces synced, and board 0's pane collapsed.

- [ ] **Step 2: Make the three changes** described in the spec's *Cross-board drag* section:
  1. Resolve `p_source_pane_id` through `EditorBoardStrip::find_leaf_by_id()` when the local `get_leaf_by_id()` misses.
  2. Relax `ERR_FAIL_COND_V(!leaves.has(p_target_leaf), nullptr)` at `editor/editor_scene_workspace.cpp:145` to accept a target owned by any board.
  3. After a cross-board move, call `sync_scene_tabs_from_editor_data()` and `collapse_if_empty_deferred()` on **both** workspaces.

- [ ] **Step 3: Apply the same three changes to `handle_tab_strip_drop`** (`:214`), which has the same structure and the same blind spot.

- [ ] **Step 4: Run both suites and commit.**

---

### Task 12: Board reorder

**Goal:** Drag a board's caption in overview to reorder it.

**Files:**
- Modify: `editor/editor_board_strip.{h,cpp}` (`move_board`, caption drag)
- Test: `tests/editor/test_editor_board_strip.h`

**Acceptance Criteria:**
- [ ] `move_board(from, to)` reorders both the `boards` vector and the child order, and emits `board_moved` once.
- [ ] The active board remains active across a reorder, even when its index changes.
- [ ] `scroll_x` is retuned so the dragged board stays under the cursor.
- [ ] Board order round-trips through save/restore.
- [ ] No workspace state is touched by a reorder — leaf ids, tabs, and scene ownership are unchanged.

**Verify:** `./bin/foundry.* --headless test run --suite "*[Editor][Boards]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test** covering `move_board(0, 2)` with board 0 active: assert the order, that `get_active_board()` still returns the same pointer, and that its leaf ids are unchanged.

- [ ] **Step 2: Implement `move_board`** — reorder the vector, `move_child()` to match, fix up `active_index` by pointer identity rather than by index arithmetic, `queue_sort()`, emit `board_moved`.

- [ ] **Step 3: Wire caption dragging** using `EditorBoardView::index_at_point` for the drop target.

- [ ] **Step 4: Run and commit.**

**Stage 2 is complete here.** Run the strict build and the full suite.

---

## Stage 3 — Verification surfaces

### Task 13: Board-aware editor automation

**Goal:** Let an agent observe and drive boards, so every behaviour above is verifiable through the GUI.

**Files:**
- Modify: `editor/automation/editor_automation_workspace.{h,cpp}:42`
- Modify: `editor/automation/editor_automation_state.cpp:381`
- Modify: `editor/automation/editor_automation_driver.cpp` (new actions)
- Test: `tests/editor/test_editor_automation_workspace.h`

**Acceptance Criteria:**
- [ ] `read_editor_state` returns a `boards` array with one entry per board — id, title, dormant flag — plus `active_board`, with the existing `workspace` key describing the active board so no current automation consumer breaks.
- [ ] An `activate_board` action switches boards by index or by title.
- [ ] A `set_board_overview` action toggles the overview.
- [ ] A `wait_for` condition covers "board transition settled", so agents never sleep on the animation.
- [ ] The existing automation acceptance workflow passes unchanged.

**Verify:** `DISPLAY=:1 ./bin/foundry.* --headless test run --suite "*[Editor][Automation]*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test** in `tests/editor/test_editor_automation_workspace.h` asserting a three-board capture reports three entries with the right active index and dormancy flags.

- [ ] **Step 2: Change `capture_workspace_state`** to `capture_boards_state(EditorData *, EditorBoardStrip *)`, keeping the existing per-workspace function and calling it once per board.

- [ ] **Step 3: Add the two actions and the wait condition.**

- [ ] **Step 4: Run the automation suite and commit.**

---

### Task 14: Visual review gallery

**Goal:** Let a reviewer confirm the feature is correct by looking at it, without a local build.

**Files:**
- Create: gallery output only (no tracked source changes)

**Acceptance Criteria:**
- [ ] A `walkthrough` board with five ordered, captioned shots: single board; a switch caught mid-slide with the neighbour visible at the edge; overview with every board live; mid-drag with the drop rosette lit on a *different* board than the drag started in; landed, back on one board with the pane in its new home.
- [ ] Every caption states what the reviewer should confirm, not what the shot is.
- [ ] The gallery is served with `--public --basic-auth <user>:<pass>` and the URL is in the PR body.

**Verify:** Open the served URL and confirm all five shots render with captions.

**Steps:**

- [ ] **Step 1: Capture the shots** via the automation `capture_screenshot` tool, driving the editor with the actions added in Task 13.
- [ ] **Step 2: Build the board**

```sh
python3 scripts/review_gallery.py add --board boards-walkthrough --mode walkthrough \
  --caption "Confirm: one board, laid out exactly as before this feature" <shot>.png
```

- [ ] **Step 3: Serve and capture the URL**

```sh
python3 scripts/review_gallery.py serve --public --basic-auth reviewer:<password>
```

---

## Out of scope

- Detaching a board into its own OS window (`WindowWrapper` path).
- Per-board bottom panel or FileSystem dock state.
- Any migration of legacy `editor_layout.cfg` layouts — see the spec's *Persistence* section; a pre-boards config restores as a single default board.
- Any startup behaviour beyond restoring what the user had.

## Risks

1. **Restore ordering (Task 4).** The highest-severity risk: a per-board detach of the shared scene-mode surface is a use-after-free that may not crash deterministically. Mitigated by the dedicated ordering test and by keeping the global steps visibly hoisted.
2. **Leaf-id collisions (Task 1).** Silent scene-ownership corruption rather than a crash. Mitigated by making the allocator the only source of ids and testing uniqueness directly.
3. **Missed cross-board call sites (Task 6).** The three known sites are fixed; a fourth could exist in code added between now and implementation. Re-run the audit (`git grep -n "get_scene_workspace" editor/ modules/`) at implementation time and classify anything new.
4. **Overview cost on a large project.** Bounded by preview downscaling and throttled refresh, but unverified on a heavy 3D project. Measure before Stage 2 lands.
5. **Memory with many boards.** Accepted, not mitigated — see the spec. Each board holds a full set of per-tile docks.
