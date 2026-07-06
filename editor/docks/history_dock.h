/**************************************************************************/
/*  history_dock.h                                                        */
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

class CheckBox;
class ConfigFile;
class ItemList;
class EditorSceneContext;
class EditorUndoRedoManager;

namespace TestSceneWorkspace {
class TileHistoryDockTestAccess;
} // namespace TestSceneWorkspace

class HistoryDock : public EditorDock {
	FOUNDRY_CLASS(HistoryDock, EditorDock);

	friend class HistoryDockTestAccess;
	friend class TestSceneWorkspace::TileHistoryDockTestAccess;

	static inline HistoryDock *singleton = nullptr;

	EditorUndoRedoManager *ur_manager;
	ItemList *action_list = nullptr;
	EditorSceneContext *scene_context = nullptr;

	CheckBox *current_scene_checkbox = nullptr;
	CheckBox *global_history_checkbox = nullptr;

	bool need_refresh = true;
	int current_version = 0;

	void on_history_changed();
	void refresh_history();
	void on_version_changed();
	void refresh_version();
	int _get_scene_history_id() const;

protected:
	void _notification(int p_notification);

	virtual void save_layout_to_config(Ref<ConfigFile> &p_layout, const String &p_section) const override;
	virtual void load_layout_from_config(const Ref<ConfigFile> &p_layout, const String &p_section) override;

public:
	static HistoryDock *get_singleton() { return singleton; }
	static void set_focused_instance(HistoryDock *p_instance) { singleton = p_instance; }

	void seek_history(int p_index);
	void set_scene_context(EditorSceneContext *p_context);
	EditorSceneContext *get_scene_context() const { return scene_context; }

	HistoryDock(bool p_register_open_command = true);
	~HistoryDock();
};
