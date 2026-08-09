/**************************************************************************/
/*  editor_board.h                                                        */
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

#include "scene/gui/container.h"

class EditorData;
class EditorSceneWorkspace;
class EditorSelection;
class WorkspaceLeafIdAllocator;

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
	// p_allocator must be supplied by the owning strip before any leaf exists: the
	// workspace's first leaf is allocated inside this call, so injecting the shared
	// allocator afterwards would let two boards issue the same leaf id.
	static EditorBoard *create(int p_board_id, const String &p_title, EditorSelection *p_editor_selection, EditorData *p_editor_data, WorkspaceLeafIdAllocator *p_allocator = nullptr);

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
