/**************************************************************************/
/*  editor_script_node_drop.h                                            */
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

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class Node;
class Script;

/**
 * Shared node→script drop validation and reference formatting (U15b). Used by
 * ScriptTextEditor and unit tests.
 */
class EditorScriptNodeDrop {
public:
	enum DropRejectReason {
		DROP_OK = 0,
		DROP_REJECT_NO_SCRIPT_SCENE,
		DROP_REJECT_CROSS_SCENE,
		DROP_REJECT_NOT_NODE_SCRIPT,
	};

	struct DropValidation {
		DropRejectReason reason = DROP_OK;
		Node *drag_scene_root = nullptr;
		Node *script_scene_root = nullptr;
		Node *script_anchor_node = nullptr;
	};

	static DropValidation validate_nodes_drop(const Dictionary &p_drag_data, Node *p_script_associated_scene, const Ref<Script> &p_script, bool p_require_associated_scene);
	static String format_node_reference(Node *p_anchor_node, Node *p_node);
	static String build_nodes_drop_text(const Dictionary &p_drag_data, const DropValidation &p_validation, bool p_member_drop, bool p_use_type_hints);
};
