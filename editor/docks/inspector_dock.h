/**************************************************************************/
/*  inspector_dock.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/docks/editor_dock.h"
#include "editor/editor_data.h"
#include "editor/gui/create_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/tree.h"

class EditorFileDialog;
class EditorObjectSelector;
class EditorSceneContext;

class InspectorDock : public EditorDock {
	FOUNDRY_CLASS(InspectorDock, EditorDock);

	// Grants the dock-binding unit tests read access to the history-dependent
	// chrome (back/forward/history-menu button states).
	friend class InspectorDockTestAccess;

	enum MenuOptions {
		RESOURCE_LOAD,
		RESOURCE_SAVE,
		RESOURCE_SAVE_AS,
		RESOURCE_SHOW_IN_FILESYSTEM,
		RESOURCE_MAKE_BUILT_IN,
		RESOURCE_COPY,
		RESOURCE_EDIT_CLIPBOARD,
		OBJECT_COPY_PARAMS,
		OBJECT_PASTE_PARAMS,
		OBJECT_UNIQUE_RESOURCES,
		OBJECT_REQUEST_HELP,

		COLLAPSE_ALL,
		EXPAND_ALL,
		EXPAND_REVERTABLE,

		// Matches `EditorPropertyNameProcessor::Style`.
		PROPERTY_NAME_STYLE_RAW,
		PROPERTY_NAME_STYLE_CAPITALIZED,
		PROPERTY_NAME_STYLE_LOCALIZED,

		OBJECT_METHOD_BASE = 500
	};

	EditorData *editor_data = nullptr;

	// The scene context this dock is bound to. Inspector navigation history is
	// read through it rather than through EditorNode globals.
	EditorSceneContext *scene_context = nullptr;
	int owning_pane = 0;

	EditorInspector *inspector = nullptr;

	Object *current = nullptr;

	Button *backward_button = nullptr;
	Button *forward_button = nullptr;

	EditorFileDialog *load_resource_dialog = nullptr;
	CreateDialog *new_resource_dialog = nullptr;
	Button *resource_new_button = nullptr;
	Button *resource_load_button = nullptr;
	MenuButton *resource_save_button = nullptr;
	MenuButton *resource_extra_button = nullptr;
	MenuButton *history_menu = nullptr;
	LineEdit *search = nullptr;

	Button *open_docs_button = nullptr;
	MenuButton *object_menu = nullptr;
	EditorObjectSelector *object_selector = nullptr;

	bool info_is_warning = false; // Display in yellow and use warning icon if true.
	Button *info = nullptr;
	AcceptDialog *info_dialog = nullptr;

	int current_option = -1;
	ConfirmationDialog *unique_resources_confirmation = nullptr;
	Label *unique_resources_label = nullptr;
	Tree *unique_resources_list_tree = nullptr;

	EditorPropertyNameProcessor::Style property_name_style;
	List<Pair<StringName, Variant>> stored_properties;

	void _prepare_menu();
	void _menu_option(int p_option);
	void _menu_confirm_current();
	void _menu_option_confirm(int p_option, bool p_confirmed);

	void _new_resource();
	void _load_resource(const String &p_type = "");
	void _open_resource_selector() { _load_resource(); } // just used to call from arg-less signal
	void _resource_file_selected(const String &p_file);
	void _save_resource(bool save_as);
	void _unref_resource();
	void _copy_resource();
	void _paste_resource();
	void _prepare_resource_extra_popup();
	Ref<Resource> _get_current_resource() const;

	void _info_pressed();
	void _resource_created();
	void _resource_selected(const Ref<Resource> &p_res, const String &p_property);
	void _files_moved(const String &p_old_file, const String &p_new_file);
	void _edit_forward();
	void _edit_back();
	void _menu_collapseall();
	void _menu_expandall();
	void _menu_expand_revertable();
	void _select_history(int p_idx);
	void _prepare_history();

	// Inspector navigation history of the bound context (null when no context
	// is bound).
	EditorSelectionHistory *_get_history() const;

	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;
	virtual void input(const Ref<InputEvent> &p_event) override;

private:
	static inline InspectorDock *singleton = nullptr;

public:
	// Both accessors return the dock/inspector bound to the focused scene
	// context. Call sites that must act on a specific context should hold an
	// explicit dock/context reference instead of relying on these
	// focused-context singletons.
	static InspectorDock *get_singleton() { return singleton; }
	static void set_focused_instance(InspectorDock *p_instance) { singleton = p_instance; }
	static EditorInspector *get_inspector_singleton() { return singleton ? singleton->inspector : nullptr; }

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void go_back();
	void edit_resource(const Ref<Resource> &p_resource);
	void open_resource(const String &p_type);
	void clear();
	void set_info(const String &p_button_text, const String &p_message, bool p_is_warning);
	void set_scene_context(EditorSceneContext *p_context);
	void set_owning_pane(int p_pane) { owning_pane = p_pane; }
	EditorSceneContext *get_scene_context() const { return scene_context; }
	void update(Object *p_object);
	Container *get_addon_area();
	EditorInspector *get_inspector() { return inspector; }

	EditorPropertyNameProcessor::Style get_property_name_style() const;

	void store_script_properties(Object *p_object);
	void apply_script_properties(Object *p_object);

	InspectorDock(EditorData &p_editor_data, bool p_register_open_command = true);
	~InspectorDock();
};
