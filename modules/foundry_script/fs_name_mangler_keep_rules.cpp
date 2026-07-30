/**************************************************************************/
/*  fs_name_mangler_keep_rules.cpp                                        */
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

#include "fs_name_mangler_keep_rules.h"

#ifdef TOOLS_ENABLED

#include "core/io/file_access.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"

namespace {

struct KeepRulesClassComparator {
	bool operator()(const FoundryScript *p_left, const FoundryScript *p_right) const {
		if (p_left->get_fully_qualified_name() != p_right->get_fully_qualified_name()) {
			return p_left->get_fully_qualified_name() < p_right->get_fully_qualified_name();
		}
		return p_left->get_script_path() < p_right->get_script_path();
	}
};

struct KeepRulesNameComparator {
	bool operator()(const StringName &p_left, const StringName &p_right) const {
		return String(p_left) < String(p_right);
	}
};

bool is_class_separator(char32_t p_character) {
	return p_character == '/' || p_character == '.' || p_character == ':';
}

bool glob_match_from(const String &p_pattern, const String &p_value, int p_pattern_index, int p_value_index,
		bool p_has_separators, HashMap<uint64_t, bool> &r_memo) {
	const uint64_t key = (uint64_t(uint32_t(p_pattern_index)) << 32) | uint32_t(p_value_index);
	const bool *cached = r_memo.getptr(key);
	if (cached != nullptr) {
		return *cached;
	}

	bool matched = false;
	if (p_pattern_index == p_pattern.length()) {
		matched = p_value_index == p_value.length();
	} else {
		const char32_t pattern_character = p_pattern[p_pattern_index];
		if (pattern_character == '*') {
			const bool double_star =
					p_pattern_index + 1 < p_pattern.length() && p_pattern[p_pattern_index + 1] == '*';
			const int next_pattern_index = p_pattern_index + (double_star ? 2 : 1);
			matched = glob_match_from(
					p_pattern, p_value, next_pattern_index, p_value_index, p_has_separators, r_memo);
			if (!matched && p_value_index < p_value.length() &&
					(double_star || !p_has_separators || !is_class_separator(p_value[p_value_index]))) {
				matched = glob_match_from(
						p_pattern, p_value, p_pattern_index, p_value_index + 1, p_has_separators, r_memo);
			}
		} else if (pattern_character == '?') {
			matched = p_value_index < p_value.length() &&
					(!p_has_separators || !is_class_separator(p_value[p_value_index])) &&
					glob_match_from(
							p_pattern, p_value, p_pattern_index + 1, p_value_index + 1, p_has_separators, r_memo);
		} else {
			matched = p_value_index < p_value.length() && pattern_character == p_value[p_value_index] &&
					glob_match_from(
							p_pattern, p_value, p_pattern_index + 1, p_value_index + 1, p_has_separators, r_memo);
		}
	}

	r_memo.insert(key, matched);
	return matched;
}

bool glob_matches(const String &p_pattern, const String &p_value, bool p_has_separators) {
	HashMap<uint64_t, bool> memo;
	return glob_match_from(p_pattern, p_value, 0, 0, p_has_separators, memo);
}

String strip_comment(const String &p_line) {
	const int comment = p_line.find_char('#');
	return (comment < 0 ? p_line : p_line.substr(0, comment)).strip_edges();
}

bool validate_glob(const String &p_pattern, bool p_class_pattern, String &r_error) {
	if (p_pattern.is_empty()) {
		r_error = p_class_pattern ? "Class glob cannot be empty." : "Member glob cannot be empty.";
		return false;
	}
	if (p_pattern.find("***") >= 0) {
		r_error = "Only `*` and `**` wildcards are supported.";
		return false;
	}
	for (int i = 0; i < p_pattern.length(); i++) {
		const char32_t character = p_pattern[i];
		if (is_whitespace(character)) {
			r_error = p_class_pattern ? "Class glob cannot contain whitespace." : "Member entries cannot contain types, modifiers, or argument lists.";
			return false;
		}
		if (character == '{' || character == '}' || character == ';' || character == ',' ||
				character == '!' || character == '(' || character == ')' || character == '[' ||
				character == ']' || character == '\\') {
			r_error = "Unsupported syntax in glob `" + p_pattern + "`.";
			return false;
		}
		if (!p_class_pattern && is_class_separator(character)) {
			r_error = "Member globs must be atomic declaration names.";
			return false;
		}
	}
	return true;
}

void append_diagnostic(Vector<FSNameManglerKeepRules::Diagnostic> &r_diagnostics,
		FSNameManglerKeepRules::DiagnosticSeverity p_severity, const String &p_source, int p_line,
		const String &p_message) {
	FSNameManglerKeepRules::Diagnostic diagnostic;
	diagnostic.severity = p_severity;
	diagnostic.source = p_source;
	diagnostic.line = p_line;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
}

String rule_key(int p_directive, const String &p_class_pattern, const Vector<String> &p_member_patterns) {
	String key = String::num_int64(p_directive) + "\n" + p_class_pattern;
	for (const String &member_pattern : p_member_patterns) {
		key += "\n" + member_pattern;
	}
	return key;
}

void collect_classes(const FoundryScript *p_class, Vector<const FoundryScript *> &r_classes,
		HashSet<const FoundryScript *> &r_seen) {
	if (p_class == nullptr || r_seen.has(p_class)) {
		return;
	}
	r_seen.insert(p_class);
	r_classes.push_back(p_class);
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->get_subclasses()) {
		collect_classes(subclass.value.ptr(), r_classes, r_seen);
	}
}

} // namespace

Vector<StringName> FSNameManglerKeepRules::_collect_member_names(const FoundryScript *p_class) {
	HashSet<StringName> unique_names;
	for (const StringName &member : p_class->members) {
		unique_names.insert(member);
	}
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &member : p_class->static_variables_indices) {
		unique_names.insert(member.key);
	}
	for (const KeyValue<StringName, Variant> &constant : p_class->constants) {
		unique_names.insert(constant.key);
	}
	for (const KeyValue<StringName, MethodInfo> &signal : p_class->_signals) {
		unique_names.insert(signal.key);
	}
	for (const KeyValue<StringName, FSFunction *> &method : p_class->member_functions) {
		unique_names.insert(method.key);
	}
	for (const KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry : p_class->enum_functions) {
		unique_names.insert(enum_entry.key);
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.instance_functions) {
			unique_names.insert(method.key);
		}
		for (const KeyValue<StringName, FSFunction *> &method : enum_entry.value.static_functions) {
			unique_names.insert(method.key);
		}
	}
	for (const KeyValue<StringName, FoundryScript::AbstractTraitRequirement> &requirement :
			p_class->abstract_trait_requirements) {
		unique_names.insert(requirement.key);
	}

	Vector<StringName> names;
	for (const StringName &name : unique_names) {
		names.push_back(name);
	}
	names.sort_custom<KeepRulesNameComparator>();
	return names;
}

String FSNameManglerKeepRules::Diagnostic::format() const {
	return vformat("%s:%d: %s: %s", source, line,
			severity == DIAGNOSTIC_WARNING ? "warning" : "error", message);
}

Error FSNameManglerKeepRules::parse(const String &p_text, const String &p_source,
		FSNameManglerKeepRules &r_rules, Vector<Diagnostic> &r_diagnostics) {
	r_diagnostics.clear();

	Vector<Rule> parsed_rules;
	HashMap<String, int> original_rule_lines;
	const PackedStringArray lines = p_text.split("\n", true);
	for (int line_index = 0; line_index < lines.size(); line_index++) {
		const int line_number = line_index + 1;
		const String line = strip_comment(lines[line_index]);
		if (line.is_empty()) {
			continue;
		}
		if (line == "}" || line.begins_with("}")) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
					"Unexpected closing `}`.");
			return ERR_PARSE_ERROR;
		}

		Rule rule;
		rule.source = p_source;
		rule.line = line_number;
		String remainder;
		if (line.begins_with("-keep class ")) {
			rule.directive = DIRECTIVE_KEEP;
			remainder = line.trim_prefix("-keep class ").strip_edges();
		} else if (line.begins_with("-keepclassmembers class ")) {
			rule.directive = DIRECTIVE_KEEP_CLASS_MEMBERS;
			remainder = line.trim_prefix("-keepclassmembers class ").strip_edges();
		} else if (line.begins_with("-keep") || line.begins_with("-keepclassmembers")) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
					"Expected `class` after the keep-rule directive.");
			return ERR_PARSE_ERROR;
		} else {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
					"Unsupported keep-rule directive.");
			return ERR_PARSE_ERROR;
		}

		const int opening_brace = remainder.find_char('{');
		bool has_block = opening_brace >= 0;
		if (has_block) {
			if (opening_brace != remainder.length() - 1 || remainder.find_char('}') >= 0) {
				append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
						"Inline keep-rule blocks are not supported; `{` must end the header and `}` must be on its own line.");
				return ERR_PARSE_ERROR;
			}
			remainder = remainder.substr(0, opening_brace).strip_edges();
		}
		if (rule.directive == DIRECTIVE_KEEP_CLASS_MEMBERS && !has_block) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
					"`-keepclassmembers` requires a member block.");
			return ERR_PARSE_ERROR;
		}

		String validation_error;
		if (!validate_glob(remainder, true, validation_error)) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number, validation_error);
			return ERR_PARSE_ERROR;
		}
		rule.class_pattern = remainder;

		if (has_block) {
			bool closed = false;
			for (line_index++; line_index < lines.size(); line_index++) {
				const int member_line_number = line_index + 1;
				const String member_line = strip_comment(lines[line_index]);
				if (member_line.is_empty()) {
					continue;
				}
				if (member_line == "}") {
					closed = true;
					break;
				}
				if (member_line.find_char('{') >= 0 || member_line.find_char('}') >= 0) {
					append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, member_line_number,
							"Member blocks require one glob per line and `}` on its own line.");
					return ERR_PARSE_ERROR;
				}
				if (!member_line.ends_with(";")) {
					append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, member_line_number,
							"Each member glob must end with `;`.");
					return ERR_PARSE_ERROR;
				}
				const String member_pattern =
						member_line.substr(0, member_line.length() - 1).strip_edges();
				if (!validate_glob(member_pattern, false, validation_error)) {
					append_diagnostic(
							r_diagnostics, DIAGNOSTIC_ERROR, p_source, member_line_number, validation_error);
					return ERR_PARSE_ERROR;
				}
				rule.member_patterns.push_back(member_pattern);
			}
			if (!closed) {
				append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
						"Missing closing `}` for member block.");
				return ERR_PARSE_ERROR;
			}
			if (rule.member_patterns.is_empty()) {
				append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_source, line_number,
						"Keep-rule member block must contain at least one member pattern.");
				return ERR_PARSE_ERROR;
			}
		}

		const String key = rule_key(rule.directive, rule.class_pattern, rule.member_patterns);
		const int *original_line = original_rule_lines.getptr(key);
		if (original_line != nullptr) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_WARNING, p_source, line_number,
					vformat("Duplicate keep rule; first declared at %s:%d.", p_source, *original_line));
			continue;
		}
		original_rule_lines.insert(key, line_number);
		parsed_rules.push_back(rule);
	}

	r_rules.rules = parsed_rules;
	return OK;
}

Error FSNameManglerKeepRules::load(const String &p_path, FSNameManglerKeepRules &r_rules,
		Vector<Diagnostic> &r_diagnostics) {
	r_diagnostics.clear();

	Error open_error = OK;
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &open_error);
	if (file.is_null()) {
		append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_path, 1,
				vformat("Could not read keep-rules file (error %d).", open_error));
		return open_error;
	}

	const uint64_t length = file->get_length();
	if (length > uint64_t(INT32_MAX)) {
		append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_path, 1,
				"Could not read keep-rules file: file is too large.");
		return ERR_OUT_OF_MEMORY;
	}

	Vector<uint8_t> bytes;
	const Error resize_error = bytes.resize((int)length);
	if (resize_error != OK) {
		append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_path, 1,
				"Could not read keep-rules file: unable to allocate the file buffer.");
		return resize_error;
	}
	if (length > 0 && file->get_buffer(bytes.ptrw(), length) != length) {
		append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_path, 1,
				"Could not read keep-rules file.");
		return ERR_FILE_CANT_READ;
	}

	String text;
	if (length > 0) {
		const Error decode_error =
				text.append_utf8(reinterpret_cast<const char *>(bytes.ptr()), (int)length);
		if (decode_error != OK) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_ERROR, p_path, 1,
					"Could not read keep-rules file: invalid UTF-8.");
			return decode_error;
		}
	}

	FSNameManglerKeepRules parsed_rules;
	const Error parse_error = parse(text, p_path, parsed_rules, r_diagnostics);
	if (parse_error != OK) {
		return parse_error;
	}
	r_rules = parsed_rules;
	return OK;
}

Error FSNameManglerKeepRules::apply_to_input(
		FSNameManglerAnalysis::Input &r_input, Vector<Diagnostic> &r_diagnostics) const {
	Vector<const FoundryScript *> classes;
	HashSet<const FoundryScript *> seen;
	for (const Ref<FoundryScript> &script : r_input.scripts) {
		if (script.is_null()) {
			return ERR_INVALID_PARAMETER;
		}
		collect_classes(script.ptr(), classes, seen);
	}
	classes.sort_custom<KeepRulesClassComparator>();

	for (const Rule &rule : rules) {
		int matched_declarations = 0;
		const String directive =
				rule.directive == DIRECTIVE_KEEP ? "-keep" : "-keepclassmembers";
		for (const FoundryScript *script_class : classes) {
			const String identity = script_class->get_fully_qualified_name();
			if (!glob_matches(rule.class_pattern, identity, true)) {
				continue;
			}

			if (rule.directive == DIRECTIVE_KEEP) {
				matched_declarations++;
				const String detail =
						vformat("%s:%d %s matched class %s", rule.source, rule.line, directive, identity);
				const StringName local_name = script_class->get_local_name();
				if (local_name != StringName()) {
					r_input.add_keep(local_name, FSNameManglerAnalysis::KEEP_RULE, detail);
				}
			}

			const Vector<StringName> member_names = _collect_member_names(script_class);
			for (const StringName &member_name : member_names) {
				for (const String &member_pattern : rule.member_patterns) {
					if (!glob_matches(member_pattern, String(member_name), false)) {
						continue;
					}
					matched_declarations++;
					r_input.add_keep(member_name, FSNameManglerAnalysis::KEEP_RULE,
							vformat("%s:%d %s matched member %s::%s", rule.source, rule.line,
									directive, identity, member_name));
					break;
				}
			}
		}

		if (matched_declarations == 0 && r_input.complete_project_graph) {
			append_diagnostic(r_diagnostics, DIAGNOSTIC_WARNING, rule.source, rule.line,
					vformat("Keep rule `%s class %s` matched no declarations.",
							directive, rule.class_pattern));
		}
	}
	return OK;
}

#endif // TOOLS_ENABLED
