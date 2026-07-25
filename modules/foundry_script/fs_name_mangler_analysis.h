/**************************************************************************/
/*  fs_name_mangler_analysis.h                                            */
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

#include "foundry_script.h"

#include "core/error/error_list.h"
#include "core/templates/rb_map.h"
#include "core/templates/vector.h"

#ifdef TOOLS_ENABLED

// Read-only whole-program policy pass for the compiled-bytecode name mangler. Later export stages
// supply external keep evidence and consume the ordered map; this class never mutates a script.
class FSNameManglerAnalysis {
public:
	enum IdentifierKind {
		IDENTIFIER_CLASS,
		IDENTIFIER_MEMBER,
		IDENTIFIER_METHOD,
		IDENTIFIER_SIGNAL,
		IDENTIFIER_ENUM_OR_CONSTANT,
	};

	enum KeepReason {
		KEEP_SCENE_OR_RESOURCE,
		KEEP_RPC,
		KEEP_NATIVE_VIRTUAL,
		KEEP_STRING_LITERAL,
		KEEP_REFLECTION,
		KEEP_EXTERNAL_OR_UNPROVABLE,
		KEEP_RULE,
	};

	struct KeepEvidence {
		StringName name;
		KeepReason reason = KEEP_EXTERNAL_OR_UNPROVABLE;
		String detail;
	};

	struct Input {
		Vector<Ref<FoundryScript>> scripts;
		Vector<KeepEvidence> keep_evidence;
		bool complete_project_graph = true;

		void add_keep(const StringName &p_name, KeepReason p_reason, const String &p_detail = String());
	};

	struct Classification {
		StringName name;
		Vector<IdentifierKind> kinds;
		Vector<KeepEvidence> keep_evidence;
		StringName replacement;

		bool is_kept() const;
	};

	struct Result {
		Error error = OK;
		Vector<Classification> classifications;
		RBMap<StringName, StringName> rename_map;
		Vector<String> keep_log;

		const Classification *find(const StringName &p_name) const;
	};

	static Result analyze(const Input &p_input);
	static String get_keep_reason_label(KeepReason p_reason);

private:
	struct BuildState;
};

#endif // TOOLS_ENABLED
