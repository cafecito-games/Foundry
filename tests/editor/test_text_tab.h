/**************************************************************************/
/*  test_text_tab.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "editor/workspace/text_document.h"
#include "editor/workspace/text_tab.h"
#include "editor/workspace/text_view.h"
#include "editor/workspace/text_view_registry.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_registry.h"

#include "tests/editor/test_scene_workspace.h"
#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestTextTab {

// A second, non-Source view mode used only to exercise the view-mode extension
// point: registering it for an extension must make both Source and this view
// available, with no change to TextTabType.
class FakePreviewTextView : public TextView {
public:
	static Ref<TextView> create(const Ref<TextDocument> &p_document) {
		Ref<FakePreviewTextView> view;
		view.instantiate();
		view->set_document(p_document);
		return view;
	}

	StringName mode_id() const override { return StringName("fake_preview"); }
	String label() const override { return "Preview"; }
	Control *get_control() override { return nullptr; }
	void sync_from_document() override {}
	void flush_to_document() override {}
	bool is_editable() const override { return true; }
};

static String write_text_file(const String &p_name, const String &p_contents) {
	const String dir = OS::get_singleton()->get_cache_path().path_join("text_tab_tests");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	da->make_dir_recursive(dir);
	const String path = dir.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(p_contents);
	}
	return path;
}

static int count_tabs_of_type(EditorSceneWorkspace *p_workspace, const StringName &p_type) {
	int total = 0;
	for (WorkspaceLeafNode *leaf : p_workspace->get_leaves()) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane) {
			continue;
		}
		for (int i = 0; i < pane->get_tab_count(); i++) {
			if (pane->get_tab(i).get_type_id() == p_type) {
				total++;
			}
		}
	}
	return total;
}

// Locates the pane/index/stable_id of the first text tab in the workspace.
static bool find_text_tab(EditorSceneWorkspace *p_workspace, WorkspacePane **r_pane, int *r_index, int *r_stable_id) {
	for (WorkspaceLeafNode *leaf : p_workspace->get_leaves()) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (!pane) {
			continue;
		}
		for (int i = 0; i < pane->get_tab_count(); i++) {
			if (pane->get_tab(i).get_type_id() == StringName("text")) {
				*r_pane = pane;
				*r_index = i;
				*r_stable_id = pane->get_tab(i).get_stable_id();
				return true;
			}
		}
	}
	return false;
}

static TextTabType *shared_text_type() {
	return static_cast<TextTabType *>(WorkspacePane::get_shared_tab_registry().find_type(StringName("text")));
}

TEST_CASE("[text-tab] text-view-registry-source-default") {
	TextViewRegistry registry;

	// Every extension offers the built-in Source view.
	Vector<TextViewRegistry::ViewEntry> plain_views = registry.get_views_for_path("res://notes.txt");
	REQUIRE(plain_views.size() == 1);
	CHECK(plain_views[0].mode_id == StringName("source"));
	CHECK(registry.default_mode_for_path("res://any.weirdext") == StringName("source"));

	// Registering a second view for an extension makes both available -- driving
	// the extension point without a real preview and without touching TextTabType.
	registry.register_view("md", StringName("fake_preview"), "Preview", &FakePreviewTextView::create);

	Vector<TextViewRegistry::ViewEntry> md_views = registry.get_views_for_path("res://readme.md");
	REQUIRE(md_views.size() == 2);
	CHECK(md_views[0].mode_id == StringName("source"));
	CHECK(md_views[1].mode_id == StringName("fake_preview"));
	CHECK(registry.has_view("res://readme.md", StringName("fake_preview")));
	// The extra view is scoped to its extension; plain text still offers only Source.
	CHECK_FALSE(registry.has_view("res://notes.txt", StringName("fake_preview")));
}

TEST_CASE("[text-tab] text-tab-title-is-filename") {
	TextTabType type;
	WorkspaceTab tab = type.make_tab("res://docs/README.md", 1);
	CHECK(type.get_title(tab) == "README.md");
	CHECK(tab.get_type_id() == StringName("text"));
	CHECK(tab.get_resource_key() == "res://docs/README.md");
}

TEST_CASE("[text-tab] text-tab-persist-roundtrip") {
	WorkspaceTabRegistry registry;
	WorkspaceTabType *text_type = registry.find_type(StringName("text"));
	REQUIRE(text_type != nullptr);

	WorkspaceTab original = text_type->make_tab("res://notes.md", 7);
	Dictionary payload;
	payload["view_mode"] = StringName("source");
	payload["caret_line"] = 12;
	payload["caret_column"] = 3;
	payload["scroll"] = 40;
	original.set_payload(payload);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "Tab_7";
	original.save_to_config(config, section, text_type);

	WorkspaceTab restored;
	restored.load_from_config(config, section, text_type);
	CHECK(restored == original);

	// File-backed: contents are never serialized into the layout (reloaded from disk).
	const Dictionary &restored_payload = restored.get_payload();
	CHECK_FALSE(restored_payload.has("text"));
	CHECK_FALSE(restored_payload.has("contents"));
	CHECK(int(restored_payload["caret_line"]) == 12);
	CHECK(int(restored_payload["scroll"]) == 40);
}

TEST_CASE("[text-tab] text-tab-missing-file-dropped-availability") {
	TextTabType type;

	const String existing = write_text_file("available.txt", "content");
	WorkspaceTab present_tab = type.make_tab(existing, 1);
	CHECK(type.is_resource_available(present_tab));

	WorkspaceTab missing_tab = type.make_tab(existing + ".gone", 2);
	CHECK_FALSE(type.is_resource_available(missing_tab));
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-open-reveal") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path = write_text_file("reveal.md", "# Notes\n");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	WorkspaceLeafNode *first = h.workspace->open_text_tab(source, path);
	h.pump();
	REQUIRE(first != nullptr);
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 1);

	WorkspacePane *pane = nullptr;
	int index = -1;
	int stable_id = -1;
	REQUIRE(find_text_tab(h.workspace, &pane, &index, &stable_id));

	// Opening the same file again reveals the existing tab: count stays 1 and the
	// stable id is unchanged.
	WorkspaceLeafNode *second = h.workspace->open_text_tab(source, path);
	h.pump();
	CHECK(second == first);
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 1);

	WorkspacePane *pane2 = nullptr;
	int index2 = -1;
	int stable_id2 = -1;
	REQUIRE(find_text_tab(h.workspace, &pane2, &index2, &stable_id2));
	CHECK(stable_id2 == stable_id);

	h.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-per-file-distinct") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path_a = write_text_file("distinct_a.txt", "alpha");
	const String path_b = write_text_file("distinct_b.txt", "beta");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	REQUIRE(source != nullptr);

	h.workspace->open_text_tab(source, path_a);
	h.pump();
	WorkspaceLeafNode *host_leaf = h.workspace->open_text_tab(source, path_b);
	h.pump();

	// Two different files -> two distinct text tabs.
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 2);

	WorkspacePane *host_pane = host_leaf->get_workspace_pane();
	REQUIRE(host_pane != nullptr);
	REQUIRE(host_pane->get_tab_count() == 2);

	// One can move to a split pane via the generic drop path.
	int move_index = -1;
	for (int i = 0; i < host_pane->get_tab_count(); i++) {
		if (host_pane->get_tab(i).get_resource_key() == path_b) {
			move_index = i;
			break;
		}
	}
	REQUIRE(move_index >= 0);
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(host_leaf->get_leaf_id(), move_index, host_leaf, EditorSceneWorkspace::DROP_RIGHT);
	h.pump();
	REQUIRE(dest != nullptr);
	CHECK(dest != host_leaf);
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 2);

	h.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-dirty-close-prompts") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path = write_text_file("dirty.txt", "original");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	h.workspace->open_text_tab(source, path);
	h.pump();

	WorkspacePane *pane = nullptr;
	int index = -1;
	int stable_id = -1;
	REQUIRE(find_text_tab(h.workspace, &pane, &index, &stable_id));

	TextTabType *text_type = shared_text_type();
	REQUIRE(text_type != nullptr);

	// Editing the backing document marks it dirty; closing then defers to a prompt.
	Ref<TextDocument> document = text_type->get_document_for(stable_id);
	REQUIRE(document.is_valid());
	document->set_text("edited contents");
	CHECK(document->is_dirty());

	WorkspaceTab dirty_tab = pane->get_tab(index);
	CHECK(text_type->request_close(dirty_tab) == WorkspaceTabCloseResult::DEFERRED);

	// A clean tab closes immediately (no deferred prompt).
	document->mark_clean();
	WorkspaceTab clean_tab = pane->get_tab(index);
	CHECK(text_type->request_close(clean_tab) == WorkspaceTabCloseResult::CLOSE);

	// Close the tab through the pane so the surface/document are torn down cleanly.
	pane->request_close_tab(index);
	h.pump();

	h.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-persist-roundtrip-reloads-from-disk") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path = write_text_file("persist.md", "# Persisted\nbody\n");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *text_leaf = h.workspace->open_text_tab(source, path);
	h.pump();
	const int leaf_id = text_leaf->get_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	int text_index = -1;
	for (int i = 0; i < restored->get_tab_count(); i++) {
		if (restored->get_tab(i).get_type_id() == StringName("text")) {
			text_index = i;
			break;
		}
	}
	REQUIRE(text_index >= 0);
	CHECK(restored->get_tab(text_index).get_resource_key() == path);
	// Contents are not serialized in the layout; the tab reloads from disk.
	const Dictionary &payload = restored->get_tab(text_index).get_payload();
	CHECK_FALSE(payload.has("text"));

	h2.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-missing-file-dropped") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path = write_text_file("to_delete.txt", "temporary");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *text_leaf = h.workspace->open_text_tab(source, path);
	h.pump();
	const int leaf_id = text_leaf->get_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	// Delete the backing file so the persisted tab has no resource on restore.
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	da->remove(path);
	CHECK_FALSE(FileAccess::exists(path));

	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	ErrorDetector error_detector;
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	// The deleted file's tab is dropped with a diagnostic.
	CHECK(error_detector.has_error);
	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	if (restored) {
		CHECK(count_tabs_of_type(h2.workspace, StringName("text")) == 0);
	}

	h2.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-owns-nonscript") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String text_path = write_text_file("owned.md", "# Owned\n");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();

	// A non-script text file opens as a "text" tab.
	h.workspace->open_text_tab(source, text_path);
	h.pump();
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 1);

	// A script open does not produce a text tab: the script editor still owns it.
	h.workspace->open_script_leaf(source, "res://player.fs");
	h.pump();
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 1);

	h.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-unsaved-documents-reported-and-saved") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	const String path = write_text_file("unsaved.txt", "before");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	h.workspace->open_text_tab(source, path);
	h.pump();

	WorkspacePane *pane = nullptr;
	int index = -1;
	int stable_id = -1;
	REQUIRE(find_text_tab(h.workspace, &pane, &index, &stable_id));

	TextTabType *text_type = shared_text_type();
	REQUIRE(text_type != nullptr);

	// No unsaved documents initially.
	CHECK(text_type->get_unsaved_document_paths().is_empty());

	// Editing marks the document dirty; it is reported for the editor-wide quit /
	// save-all flow so the edit is not silently lost.
	Ref<TextDocument> document = text_type->get_document_for(stable_id);
	REQUIRE(document.is_valid());
	document->set_text("after edit");
	PackedStringArray unsaved = text_type->get_unsaved_document_paths();
	REQUIRE(unsaved.size() == 1);
	CHECK(unsaved[0] == path);

	// Save-all writes the document to disk and clears the dirty state.
	text_type->save_all_documents();
	CHECK_FALSE(document->is_dirty());
	CHECK(text_type->get_unsaved_document_paths().is_empty());

	Ref<FileAccess> reader = FileAccess::open(path, FileAccess::READ);
	REQUIRE(reader.is_valid());
	CHECK(reader->get_as_text() == "after edit");

	pane->request_close_tab(index);
	h.pump();
	h.unmount();
}

TEST_CASE("[text-tab][SceneTree][Editor] text-tab-new-file-is-path-backed") {
	using namespace TestSceneWorkspace;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	// The "New Text Document" flow writes an empty file to disk first, so the tab
	// is file-backed (identity = path) from birth rather than a synthetic scratch id.
	const String path = write_text_file("created.txt", "");
	CHECK(FileAccess::exists(path));

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *source = h.workspace->get_focused_leaf();
	h.workspace->open_text_tab(source, path);
	h.pump();

	WorkspacePane *pane = nullptr;
	int index = -1;
	int stable_id = -1;
	REQUIRE(find_text_tab(h.workspace, &pane, &index, &stable_id));
	CHECK(pane->get_tab(index).get_resource_key() == path);

	// It dedups/persists like any file tab: reopening reveals rather than duplicates.
	h.workspace->open_text_tab(source, path);
	h.pump();
	CHECK(count_tabs_of_type(h.workspace, StringName("text")) == 1);

	h.unmount();
}

} // namespace TestTextTab
