/**************************************************************************/
/*  fs_name_mangler_keep_rules.h                                         */
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

#include "fs_name_mangler_analysis.h"

#ifdef TOOLS_ENABLED

class FSNameManglerKeepRules {
public:
	enum DiagnosticSeverity {
		DIAGNOSTIC_WARNING,
		DIAGNOSTIC_ERROR,
	};

	struct Diagnostic {
		DiagnosticSeverity severity = DIAGNOSTIC_ERROR;
		String source;
		int line = 1;
		String message;

		String format() const;
	};

	static Error parse(const String &p_text, const String &p_source,
			FSNameManglerKeepRules &r_rules, Vector<Diagnostic> &r_diagnostics);
	static Error load(const String &p_path, FSNameManglerKeepRules &r_rules,
			Vector<Diagnostic> &r_diagnostics);

	Error apply_to_input(FSNameManglerAnalysis::Input &r_input,
			Vector<Diagnostic> &r_diagnostics) const;

	int get_rule_count() const { return rules.size(); }

private:
	enum Directive {
		DIRECTIVE_KEEP,
		DIRECTIVE_KEEP_CLASS_MEMBERS,
	};

	struct Rule {
		Directive directive = DIRECTIVE_KEEP;
		String class_pattern;
		Vector<String> member_patterns;
		String source;
		int line = 1;
	};

	Vector<Rule> rules;

	static Vector<StringName> _collect_member_names(const FoundryScript *p_class);
};

#endif // TOOLS_ENABLED
