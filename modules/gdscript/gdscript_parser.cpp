/**************************************************************************/
/*  gdscript_parser.cpp                                                   */
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

#include "gdscript_parser.h"

#include "gdscript.h"
#include "gdscript_cache.h"
#include "gdscript_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/core_constants.h"
#include "core/io/resource_loader.h"
#include "core/math/math_defs.h"
#include "scene/main/multiplayer_api.h"

#ifdef DEBUG_ENABLED
#include "core/string/string_builder.h"
#include "servers/text/text_server.h"
#endif

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#endif

// This function is used to determine that a type is "built-in" as opposed to native
// and custom classes. So `Variant::NIL` and `Variant::OBJECT` are excluded:
// `Variant::NIL` - `null` is literal, not a type.
// `Variant::OBJECT` - `Object` should be treated as a class, not as a built-in type.
static HashMap<StringName, Variant::Type> builtin_types;
Variant::Type GDScriptParser::get_builtin_type(const StringName &p_type) {
	if (unlikely(builtin_types.is_empty())) {
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			Variant::Type type = (Variant::Type)i;
			if (type != Variant::NIL && type != Variant::OBJECT) {
				builtin_types[Variant::get_type_name(type)] = type;
			}
		}
	}

	if (builtin_types.has(p_type)) {
		return builtin_types[p_type];
	}
	return Variant::VARIANT_MAX;
}

#ifdef DEBUG_ENABLED
bool GDScriptParser::is_project_ignoring_warnings = false;
GDScriptWarning::WarnLevel GDScriptParser::warning_levels[GDScriptWarning::WARNING_MAX];
LocalVector<GDScriptParser::WarningDirectoryRule> GDScriptParser::warning_directory_rules;
#endif // DEBUG_ENABLED

#ifdef TOOLS_ENABLED
HashMap<String, String> GDScriptParser::theme_color_names;
#endif // TOOLS_ENABLED

HashMap<StringName, GDScriptParser::AnnotationInfo> GDScriptParser::valid_annotations;

namespace {
// Increments a depth counter on construction and decrements it on destruction so
// every return path out of a recursive parse function stays balanced.
struct RecursionDepthGuard {
	int &depth;
	explicit RecursionDepthGuard(int &p_depth) :
			depth(p_depth) { depth++; }
	~RecursionDepthGuard() { depth--; }
};
} // namespace

void GDScriptParser::cleanup() {
	builtin_types.clear();
	valid_annotations.clear();
}

void GDScriptParser::get_annotation_list(List<MethodInfo> *r_annotations) const {
	for (const KeyValue<StringName, AnnotationInfo> &E : valid_annotations) {
		r_annotations->push_back(E.value.info);
	}
}

bool GDScriptParser::annotation_exists(const String &p_annotation_name) const {
	return valid_annotations.has(p_annotation_name);
}

#ifdef DEBUG_ENABLED
void GDScriptParser::update_project_settings() {
	is_project_ignoring_warnings = !GLOBAL_GET("debug/gdscript/warnings/enable").booleanize();

	for (int i = 0; i < GDScriptWarning::WARNING_MAX; i++) {
		const String setting_path = GDScriptWarning::get_setting_path_from_code((GDScriptWarning::Code)i);
		warning_levels[i] = (GDScriptWarning::WarnLevel)(int)GLOBAL_GET(setting_path);
	}

#ifndef DISABLE_DEPRECATED
	// We do not use `GLOBAL_GET`, since we check without taking overrides into account. We leave the migration of non-trivial configurations to the user.
	if (unlikely(ProjectSettings::get_singleton()->has_setting("debug/gdscript/warnings/exclude_addons"))) {
		const bool is_excluding_addons = ProjectSettings::get_singleton()->get_setting("debug/gdscript/warnings/exclude_addons", true).booleanize();
		ProjectSettings::get_singleton()->clear("debug/gdscript/warnings/exclude_addons");

		Dictionary rules = ProjectSettings::get_singleton()->get_setting("debug/gdscript/warnings/directory_rules");
		rules["res://addons"] = is_excluding_addons ? WarningDirectoryRule::DECISION_EXCLUDE : WarningDirectoryRule::DECISION_INCLUDE;
		ProjectSettings::get_singleton()->set_setting("debug/gdscript/warnings/directory_rules", rules);
	}
#endif // DISABLE_DEPRECATED

	warning_directory_rules.clear();

	const Dictionary rules = GLOBAL_GET("debug/gdscript/warnings/directory_rules");
	for (const KeyValue<Variant, Variant> &kv : rules) {
		String dir = kv.key.operator String().simplify_path();
		ERR_CONTINUE_MSG(!dir.begins_with("res://"), R"(Paths in the project setting "debug/gdscript/warnings/directory_rules" keys must start with the "res://" prefix.)");
		if (!dir.ends_with("/")) {
			dir += '/';
		}

		const int decision = kv.value;
		ERR_CONTINUE(decision < 0 || decision >= WarningDirectoryRule::DECISION_MAX);

		warning_directory_rules.push_back({ dir, (WarningDirectoryRule::Decision)decision });
	}

	struct RuleSort {
		bool operator()(const WarningDirectoryRule &p_a, const WarningDirectoryRule &p_b) const {
			return p_a.directory_path.count("/") > p_b.directory_path.count("/");
		}
	};

	warning_directory_rules.sort_custom<RuleSort>();
}

bool GDScriptParser::invalidate_analysis_on_strict_settings_change() {
	// GDScriptAnalyzer snapshots these two flags through get_setting_with_override() (via
	// GLOBAL_GET_CACHED), so track the effective, override-aware values to decide when a change
	// actually alters analysis output. Seeded false on first call to match the analyzer defaults; a
	// project that boots with strict mode already on resolves identically on first analysis, so no
	// invalidation is needed until a value actually flips.
	static bool last_strict_null = false;
	static bool last_strict_dynamic = false;
	static bool initialized = false;

	const ProjectSettings *settings = ProjectSettings::get_singleton();
	const bool strict_null = (bool)settings->get_setting_with_override("debug/gdscript/analysis/strict_null_checks");
	const bool strict_dynamic = (bool)settings->get_setting_with_override("debug/gdscript/analysis/strict_dynamic_checks");

	const bool changed = !initialized || strict_null != last_strict_null || strict_dynamic != last_strict_dynamic;
	last_strict_null = strict_null;
	last_strict_dynamic = strict_dynamic;

	if (!initialized) {
		// First observation establishes the baseline without invalidating anything.
		initialized = true;
		return false;
	}

	if (!changed) {
		return false;
	}

	// Drop already-built parsers/scripts so the live session re-analyzes under the new flags.
	GDScriptCache::invalidate_analysis();
	return true;
}
#endif // DEBUG_ENABLED

GDScriptParser::GDScriptParser() {
	// Register valid annotations.
	if (unlikely(valid_annotations.is_empty())) {
		// Script annotations.
		register_annotation(MethodInfo("@tool"), AnnotationInfo::SCRIPT, &GDScriptParser::tool_annotation);
		register_annotation(MethodInfo("@icon", PropertyInfo(Variant::STRING, "icon_path")), AnnotationInfo::SCRIPT, &GDScriptParser::icon_annotation);
		register_annotation(MethodInfo("@static_unload"), AnnotationInfo::SCRIPT, &GDScriptParser::static_unload_annotation);
		register_annotation(
				MethodInfo("@autoload", PropertyInfo(Variant::ARRAY, "depends_on"), PropertyInfo(Variant::INT, "order_id")),
				AnnotationInfo::SCRIPT,
				&GDScriptParser::autoload_annotation,
				varray(Array(), 0));
		register_annotation(MethodInfo("@noreturn"), AnnotationInfo::FUNCTION, &GDScriptParser::noreturn_annotation);
		// Onready annotation.
		register_annotation(MethodInfo("@onready"), AnnotationInfo::VARIABLE, &GDScriptParser::onready_annotation);
		// Export annotations.
		register_annotation(MethodInfo("@export"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_NONE, Variant::NIL>);
		register_annotation(MethodInfo("@export_enum", PropertyInfo(Variant::STRING, "names")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_ENUM, Variant::NIL>, varray(), true);
		register_annotation(MethodInfo("@export_file", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_FILE, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_file_path", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_FILE_PATH, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_dir"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_DIR, Variant::STRING>);
		register_annotation(MethodInfo("@export_global_file", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_GLOBAL_FILE, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_global_dir"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_GLOBAL_DIR, Variant::STRING>);
		register_annotation(MethodInfo("@export_multiline", PropertyInfo(Variant::STRING, "hint")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_MULTILINE_TEXT, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_placeholder", PropertyInfo(Variant::STRING, "placeholder")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_PLACEHOLDER_TEXT, Variant::STRING>);
		register_annotation(MethodInfo("@export_range", PropertyInfo(Variant::FLOAT, "min"), PropertyInfo(Variant::FLOAT, "max"), PropertyInfo(Variant::FLOAT, "step"), PropertyInfo(Variant::STRING, "extra_hints")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_RANGE, Variant::FLOAT>, varray(1.0, ""), true);
		register_annotation(MethodInfo("@export_exp_easing", PropertyInfo(Variant::STRING, "hints")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_EXP_EASING, Variant::FLOAT>, varray(""), true);
		register_annotation(MethodInfo("@export_color_no_alpha"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_COLOR_NO_ALPHA, Variant::COLOR>);
		register_annotation(MethodInfo("@export_node_path", PropertyInfo(Variant::STRING, "type")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_NODE_PATH_VALID_TYPES, Variant::NODE_PATH>, varray(""), true);
		register_annotation(MethodInfo("@export_flags", PropertyInfo(Variant::STRING, "names")), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_FLAGS, Variant::INT>, varray(), true);
		register_annotation(MethodInfo("@export_flags_2d_render"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_2D_RENDER, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_2d_physics"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_2D_PHYSICS, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_2d_navigation"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_2D_NAVIGATION, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_render"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_3D_RENDER, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_physics"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_3D_PHYSICS, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_navigation"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_3D_NAVIGATION, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_avoidance"), AnnotationInfo::VARIABLE, &GDScriptParser::export_annotations<PROPERTY_HINT_LAYERS_AVOIDANCE, Variant::INT>);
		register_annotation(MethodInfo("@export_storage"), AnnotationInfo::VARIABLE, &GDScriptParser::export_storage_annotation);
		register_annotation(MethodInfo("@export_custom", PropertyInfo(Variant::INT, "hint", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_ENUM, "PropertyHint"), PropertyInfo(Variant::STRING, "hint_string"), PropertyInfo(Variant::INT, "usage", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_BITFIELD, "PropertyUsageFlags")), AnnotationInfo::VARIABLE, &GDScriptParser::export_custom_annotation, varray(PROPERTY_USAGE_DEFAULT));
		register_annotation(MethodInfo("@export_tool_button", PropertyInfo(Variant::STRING, "text"), PropertyInfo(Variant::STRING, "icon")), AnnotationInfo::VARIABLE, &GDScriptParser::export_tool_button_annotation, varray(""));
		// Export grouping annotations.
		register_annotation(MethodInfo("@export_category", PropertyInfo(Variant::STRING, "name")), AnnotationInfo::STANDALONE, &GDScriptParser::export_group_annotations<PROPERTY_USAGE_CATEGORY>);
		register_annotation(MethodInfo("@export_group", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::STRING, "prefix")), AnnotationInfo::STANDALONE, &GDScriptParser::export_group_annotations<PROPERTY_USAGE_GROUP>, varray(""));
		register_annotation(MethodInfo("@export_subgroup", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::STRING, "prefix")), AnnotationInfo::STANDALONE, &GDScriptParser::export_group_annotations<PROPERTY_USAGE_SUBGROUP>, varray(""));
		// Warning annotations.
		register_annotation(MethodInfo("@warning_ignore", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STATEMENT, &GDScriptParser::warning_ignore_annotation, varray(), true);
		register_annotation(MethodInfo("@warning_ignore_start", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::STANDALONE, &GDScriptParser::warning_ignore_region_annotations, varray(), true);
		register_annotation(MethodInfo("@warning_ignore_restore", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::STANDALONE, &GDScriptParser::warning_ignore_region_annotations, varray(), true);
		// Networking.
		// Keep in sync with `rpc_annotation()` and `SceneRPCInterface::_parse_rpc_config()`.
		register_annotation(MethodInfo("@rpc", PropertyInfo(Variant::STRING, "mode"), PropertyInfo(Variant::STRING, "sync"), PropertyInfo(Variant::STRING, "transfer_mode"), PropertyInfo(Variant::INT, "transfer_channel")), AnnotationInfo::FUNCTION, &GDScriptParser::rpc_annotation, varray("authority", "call_remote", "reliable", 0));
	}

#ifdef DEBUG_ENABLED
	for (int i = 0; i < GDScriptWarning::WARNING_MAX; i++) {
		warning_ignore_start_lines[i] = INT_MAX;
	}
#endif // DEBUG_ENABLED

#ifdef TOOLS_ENABLED
	if (unlikely(theme_color_names.is_empty())) {
		// Vectors.
		theme_color_names.insert("x", "axis_x_color");
		theme_color_names.insert("y", "axis_y_color");
		theme_color_names.insert("z", "axis_z_color");
		theme_color_names.insert("w", "axis_w_color");

		// Color.
		theme_color_names.insert("r", "axis_x_color");
		theme_color_names.insert("r8", "axis_x_color");
		theme_color_names.insert("g", "axis_y_color");
		theme_color_names.insert("g8", "axis_y_color");
		theme_color_names.insert("b", "axis_z_color");
		theme_color_names.insert("b8", "axis_z_color");
		theme_color_names.insert("a", "axis_w_color");
		theme_color_names.insert("a8", "axis_w_color");
	}
#endif // TOOLS_ENABLED
}

GDScriptParser::~GDScriptParser() {
	while (list != nullptr) {
		Node *element = list;
		list = list->next;
		memdelete(element);
	}
}

void GDScriptParser::clear() {
	GDScriptParser tmp;
	tmp = *this;
	*this = GDScriptParser();
}

void GDScriptParser::push_error(const String &p_message, const Node *p_origin) {
	// TODO: Improve error reporting by pointing at source code.
	// TODO: Errors might point at more than one place at once (e.g. show previous declaration).
	panic_mode = true;
	// TODO: Improve positional information.
	if (p_origin == nullptr) {
		errors.push_back({ p_message, previous.start_line, previous.start_column });
	} else {
		errors.push_back({ p_message, p_origin->start_line, p_origin->start_column });
	}
}

#ifdef DEBUG_ENABLED
void GDScriptParser::push_warning(const Node *p_source, GDScriptWarning::Code p_code, const Vector<String> &p_symbols) {
	ERR_FAIL_NULL(p_source);
	ERR_FAIL_INDEX(p_code, GDScriptWarning::WARNING_MAX);

	if (is_project_ignoring_warnings || is_script_ignoring_warnings) {
		return;
	}

	const GDScriptWarning::WarnLevel warn_level = warning_levels[p_code];
	if (warn_level == GDScriptWarning::IGNORE) {
		return;
	}

	PendingWarning pw;
	pw.source = p_source;
	pw.code = p_code;
	pw.treated_as_error = warn_level == GDScriptWarning::ERROR;
	pw.symbols = p_symbols;

	pending_warnings.push_back(pw);
}

void GDScriptParser::apply_pending_warnings() {
	for (const PendingWarning &pw : pending_warnings) {
		if (warning_ignored_lines[pw.code].has(pw.source->start_line)) {
			continue;
		}
		if (warning_ignore_start_lines[pw.code] <= pw.source->start_line) {
			continue;
		}

		GDScriptWarning warning;
		warning.code = pw.code;
		warning.symbols = pw.symbols;
		warning.start_line = pw.source->start_line;
		warning.end_line = pw.source->end_line;

		if (pw.treated_as_error) {
			push_error(warning.get_message() + String(" (Warning treated as error.)"), pw.source);
			continue;
		}

		List<GDScriptWarning>::Element *before = nullptr;
		for (List<GDScriptWarning>::Element *E = warnings.front(); E; E = E->next()) {
			if (E->get().start_line > warning.start_line) {
				break;
			}
			before = E;
		}
		if (before) {
			warnings.insert_after(before, warning);
		} else {
			warnings.push_front(warning);
		}
	}

	pending_warnings.clear();
}

void GDScriptParser::evaluate_warning_directory_rules_for_script_path() {
	is_script_ignoring_warnings = false;
	for (const WarningDirectoryRule &rule : warning_directory_rules) {
		if (script_path.begins_with(rule.directory_path)) {
			switch (rule.decision) {
				case WarningDirectoryRule::DECISION_EXCLUDE:
					is_script_ignoring_warnings = true;
					return; // Stop checking rules.
				case WarningDirectoryRule::DECISION_INCLUDE:
					is_script_ignoring_warnings = false;
					return; // Stop checking rules.
				case WarningDirectoryRule::DECISION_MAX:
					return; // Unreachable.
			}
		}
	}
}
#endif // DEBUG_ENABLED

void GDScriptParser::override_completion_context(const Node *p_for_node, CompletionType p_type, Node *p_node, int p_argument) {
	if (!for_completion) {
		return;
	}
	if (p_for_node == nullptr || completion_context.node != p_for_node) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.current_argument = p_argument;
	context.node = p_node;
	context.parser = this;
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void GDScriptParser::make_completion_context(CompletionType p_type, Node *p_node, int p_argument, bool p_force, const Vector<IdentifierNode *> *p_chain) {
	if (!for_completion || (!p_force && completion_context.type != COMPLETION_NONE)) {
		return;
	}
	if (previous.cursor_place != GDScriptTokenizerText::CURSOR_MIDDLE && previous.cursor_place != GDScriptTokenizerText::CURSOR_END && current.cursor_place == GDScriptTokenizerText::CURSOR_NONE) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.current_argument = p_argument;
	context.node = p_node;
	context.parser = this;
	if (p_chain != nullptr) {
		context.chain = *p_chain;
	}
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void GDScriptParser::make_completion_context(CompletionType p_type, Variant::Type p_builtin_type, bool p_force) {
	if (!for_completion || (!p_force && completion_context.type != COMPLETION_NONE)) {
		return;
	}
	if (previous.cursor_place != GDScriptTokenizerText::CURSOR_MIDDLE && previous.cursor_place != GDScriptTokenizerText::CURSOR_END && current.cursor_place == GDScriptTokenizerText::CURSOR_NONE) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.builtin_type = p_builtin_type;
	context.parser = this;
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void GDScriptParser::push_completion_call(Node *p_call) {
	if (!for_completion) {
		return;
	}
	CompletionCall call;
	call.call = p_call;
	call.argument = 0;
	completion_call_stack.push_back(call);
}

void GDScriptParser::pop_completion_call() {
	if (!for_completion) {
		return;
	}
	ERR_FAIL_COND_MSG(completion_call_stack.is_empty(), "Trying to pop empty completion call stack");
	completion_call_stack.pop_back();
}

void GDScriptParser::set_last_completion_call_arg(int p_argument) {
	if (!for_completion) {
		return;
	}
	ERR_FAIL_COND_MSG(completion_call_stack.is_empty(), "Trying to set argument on empty completion call stack");
	completion_call_stack.back()->get().argument = p_argument;
}

Error GDScriptParser::parse(const String &p_source_code, const String &p_script_path, bool p_for_completion, bool p_parse_body) {
	clear();

	String source = p_source_code;
	int cursor_line = -1;
	int cursor_column = -1;
	for_completion = p_for_completion;
	parse_body = p_parse_body;

	int tab_size = 4;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		tab_size = EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif // TOOLS_ENABLED

	if (p_for_completion) {
		// Remove cursor sentinel char.
		const Vector<String> lines = p_source_code.split("\n");
		cursor_line = 1;
		cursor_column = 1;
		for (int i = 0; i < lines.size(); i++) {
			bool found = false;
			const String &line = lines[i];
			for (int j = 0; j < line.size(); j++) {
				if (line[j] == char32_t(0xFFFF)) {
					found = true;
					break;
				} else if (line[j] == '\t') {
					cursor_column += tab_size - 1;
				}
				cursor_column++;
			}
			if (found) {
				break;
			}
			cursor_line++;
			cursor_column = 1;
		}

		source = source.replace_first(String::chr(0xFFFF), String());
	}

	GDScriptTokenizerText *text_tokenizer = memnew(GDScriptTokenizerText);
	text_tokenizer->set_source_code(source);

	tokenizer = text_tokenizer;
	tokenizer->set_cursor_position(cursor_line, cursor_column);

	script_path = p_script_path.simplify_path();

#ifdef DEBUG_ENABLED
	evaluate_warning_directory_rules_for_script_path();
#endif // DEBUG_ENABLED

	current = tokenizer->scan();
	// Avoid error or newline as the first token.
	// The latter can mess with the parser when opening files filled exclusively with comments and newlines.
	while (current.type == GDScriptTokenizer::Token::ERROR || current.type == GDScriptTokenizer::Token::NEWLINE) {
		if (current.type == GDScriptTokenizer::Token::ERROR) {
			push_error(current.literal);
		}
		current = tokenizer->scan();
	}

#ifdef DEBUG_ENABLED
	// Warn about parsing an empty script file:
	if (current.type == GDScriptTokenizer::Token::TK_EOF) {
		// Create a dummy Node for the warning, pointing to the very beginning of the file
		Node *nd = alloc_node<PassNode>();
		nd->start_line = 1;
		nd->start_column = 0;
		nd->end_line = 1;
		push_warning(nd, GDScriptWarning::EMPTY_FILE);
	}
#endif // DEBUG_ENABLED

	push_multiline(false); // Keep one for the whole parsing.
	parse_program();
	pop_multiline();

#ifdef TOOLS_ENABLED
	comment_data = tokenizer->get_comments();
#endif // TOOLS_ENABLED

	memdelete(text_tokenizer);
	tokenizer = nullptr;

#ifdef DEBUG_ENABLED
	if (multiline_stack.size() > 0) {
		ERR_PRINT("Parser bug: Imbalanced multiline stack.");
	}
#endif // DEBUG_ENABLED

	if (errors.is_empty()) {
		return OK;
	} else {
		return ERR_PARSE_ERROR;
	}
}

Error GDScriptParser::parse_binary(const Vector<uint8_t> &p_binary, const String &p_script_path) {
	GDScriptTokenizerBuffer *buffer_tokenizer = memnew(GDScriptTokenizerBuffer);
	Error err = buffer_tokenizer->set_code_buffer(p_binary);

	if (err) {
		memdelete(buffer_tokenizer);
		return err;
	}

	tokenizer = buffer_tokenizer;

	script_path = p_script_path.simplify_path();

#ifdef DEBUG_ENABLED
	evaluate_warning_directory_rules_for_script_path();
#endif // DEBUG_ENABLED

	current = tokenizer->scan();
	// Avoid error or newline as the first token.
	// The latter can mess with the parser when opening files filled exclusively with comments and newlines.
	while (current.type == GDScriptTokenizer::Token::ERROR || current.type == GDScriptTokenizer::Token::NEWLINE) {
		if (current.type == GDScriptTokenizer::Token::ERROR) {
			push_error(current.literal);
		}
		current = tokenizer->scan();
	}

	push_multiline(false); // Keep one for the whole parsing.
	parse_program();
	pop_multiline();

	memdelete(buffer_tokenizer);
	tokenizer = nullptr;

	if (errors.is_empty()) {
		return OK;
	} else {
		return ERR_PARSE_ERROR;
	}
}

GDScriptTokenizer::Token GDScriptParser::advance() {
	lambda_ended = false; // Empty marker since we're past the end in any case.

	if (current.type == GDScriptTokenizer::Token::TK_EOF) {
		ERR_FAIL_COND_V_MSG(current.type == GDScriptTokenizer::Token::TK_EOF, current, "GDScript parser bug: Trying to advance past the end of stream.");
	}
	previous = current;
	if (has_lookahead) {
		current = lookahead;
		has_lookahead = false;
	} else {
		current = tokenizer->scan();
		while (current.type == GDScriptTokenizer::Token::ERROR) {
			push_error(current.literal);
			current = tokenizer->scan();
		}
	}
	if (previous.type != GDScriptTokenizer::Token::DEDENT) { // `DEDENT` belongs to the next non-empty line.
		for (Node *n : nodes_in_progress) {
			update_extents(n);
		}
	}
	return previous;
}

// Returns the token that follows `current` without consuming `current`. The peeked token is
// buffered so the next `advance()` reuses it. Use sparingly: it scans ahead under the current
// multiline mode, so only peek when the decision is made before that mode can change.
const GDScriptTokenizer::Token &GDScriptParser::peek() {
	if (!has_lookahead) {
		lookahead = tokenizer->scan();
		while (lookahead.type == GDScriptTokenizer::Token::ERROR) {
			push_error(lookahead.literal);
			lookahead = tokenizer->scan();
		}
		has_lookahead = true;
	}
	return lookahead;
}

bool GDScriptParser::match(GDScriptTokenizer::Token::Type p_token_type) {
	if (!check(p_token_type)) {
		return false;
	}
	advance();
	return true;
}

bool GDScriptParser::check(GDScriptTokenizer::Token::Type p_token_type) const {
	if (p_token_type == GDScriptTokenizer::Token::IDENTIFIER) {
		return current.is_identifier();
	}
	return current.type == p_token_type;
}

bool GDScriptParser::consume(GDScriptTokenizer::Token::Type p_token_type, const String &p_error_message) {
	if (match(p_token_type)) {
		return true;
	}
	push_error(p_error_message);
	return false;
}

bool GDScriptParser::is_at_end() const {
	return check(GDScriptTokenizer::Token::TK_EOF);
}

void GDScriptParser::synchronize() {
	panic_mode = false;
	while (!is_at_end()) {
		if (previous.type == GDScriptTokenizer::Token::NEWLINE || previous.type == GDScriptTokenizer::Token::SEMICOLON) {
			return;
		}

		switch (current.type) {
			case GDScriptTokenizer::Token::ABSTRACT:
			case GDScriptTokenizer::Token::FINAL:
			case GDScriptTokenizer::Token::CLASS:
			case GDScriptTokenizer::Token::FUNC:
			case GDScriptTokenizer::Token::STATIC:
			case GDScriptTokenizer::Token::VAR:
			case GDScriptTokenizer::Token::TK_CONST:
			case GDScriptTokenizer::Token::SIGNAL:
			//case GDScriptTokenizer::Token::IF: // Can also be inside expressions.
			case GDScriptTokenizer::Token::FOR:
			case GDScriptTokenizer::Token::WHILE:
			case GDScriptTokenizer::Token::MATCH:
			case GDScriptTokenizer::Token::RETURN:
			case GDScriptTokenizer::Token::ANNOTATION:
				return;
			default:
				// Do nothing.
				break;
		}

		// The `async` modifier is a contextual identifier rather than a keyword token, so it is
		// not covered by the switch above. Treat it as a declaration sync point only when it
		// directly precedes a `static`/`abstract`/`func` declaration, so a recovered function
		// keeps its `is_declared_async` / `is_coroutine` metadata. A stray `async` identifier
		// inside a broken expression is left to ordinary recovery to avoid halting on a false
		// modifier and cascading into worse recovery.
		if (current.type == GDScriptTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("async")) {
			const GDScriptTokenizer::Token::Type next_type = peek().type;
			if (next_type == GDScriptTokenizer::Token::FUNC ||
					next_type == GDScriptTokenizer::Token::STATIC ||
					next_type == GDScriptTokenizer::Token::ABSTRACT ||
					next_type == GDScriptTokenizer::Token::FINAL) {
				return;
			}
		}

		advance();
	}
}

void GDScriptParser::push_multiline(bool p_state) {
	multiline_stack.push_back(p_state);
	tokenizer->set_multiline_mode(p_state);
	if (p_state) {
		// Consume potential whitespace tokens already waiting in line.
		while (current.type == GDScriptTokenizer::Token::NEWLINE || current.type == GDScriptTokenizer::Token::INDENT || current.type == GDScriptTokenizer::Token::DEDENT) {
			current = tokenizer->scan(); // Don't call advance() here, as we don't want to change the previous token.
		}
	}
}

void GDScriptParser::pop_multiline() {
	ERR_FAIL_COND_MSG(multiline_stack.is_empty(), "Parser bug: trying to pop from multiline stack without available value.");
	multiline_stack.pop_back();
	tokenizer->set_multiline_mode(multiline_stack.size() > 0 ? multiline_stack.back()->get() : false);
}

bool GDScriptParser::is_statement_end_token() const {
	return check(GDScriptTokenizer::Token::NEWLINE) || check(GDScriptTokenizer::Token::SEMICOLON) || check(GDScriptTokenizer::Token::TK_EOF);
}

bool GDScriptParser::is_statement_end() const {
	return lambda_ended || in_lambda || is_statement_end_token();
}

void GDScriptParser::end_statement(const String &p_context) {
	bool found = false;
	while (is_statement_end() && !is_at_end()) {
		// Remove sequential newlines/semicolons.
		if (is_statement_end_token()) {
			// Only consume if this is an actual token.
			advance();
		} else if (lambda_ended) {
			lambda_ended = false; // Consume this "token".
			found = true;
			break;
		} else {
			if (!found) {
				lambda_ended = true; // Mark the lambda as done since we found something else to end the statement.
				found = true;
			}
			break;
		}

		found = true;
	}
	if (!found && !is_at_end()) {
		push_error(vformat(R"(Expected end of statement after %s, found "%s" instead.)", p_context, current.get_name()));
	}
}

void GDScriptParser::parse_program() {
	head = alloc_node<ClassNode>();
	head->start_line = 1;
	head->end_line = 1;
	head->fqcn = GDScript::canonicalize_path(script_path);
	current_class = head;

	auto push_pending_annotations_to_head = [&]() {
		if (!annotation_stack.is_empty()) {
			for (AnnotationNode *annot : annotation_stack) {
				head->annotations.push_back(annot);
			}
			annotation_stack.clear();
		}
	};

	auto parse_top_level_annotations = [&](uint32_t p_valid_targets) {
		while (!check(GDScriptTokenizer::Token::TK_EOF)) {
			if (match(GDScriptTokenizer::Token::ANNOTATION)) {
				AnnotationNode *annotation = parse_annotation(p_valid_targets);
				if (annotation != nullptr) {
					if (annotation->applies_to(AnnotationInfo::CLASS)) {
						// We do not know in advance what the annotation will be applied to: the `head` class or the subsequent inner class.
						// If we encounter `class_name`, `extends` or pure `SCRIPT` annotation, then it's `head`, otherwise it's an inner class.
						annotation_stack.push_back(annotation);
					} else if (annotation->applies_to(AnnotationInfo::SCRIPT)) {
						push_pending_annotations_to_head();
						if (annotation->name == SNAME("@tool") || annotation->name == SNAME("@icon") || annotation->name == SNAME("@static_unload")) {
							// Some annotations need to be resolved and applied in the parser.
							// The root class is not in any class, so `head->outer == nullptr`.
							annotation->apply(this, head, nullptr);
						} else {
							head->annotations.push_back(annotation);
						}
					} else if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
						if (previous.type != GDScriptTokenizer::Token::NEWLINE) {
							push_error(R"(Expected newline after a standalone annotation.)");
						}
						if (annotation->name == SNAME("@export_category") || annotation->name == SNAME("@export_group") || annotation->name == SNAME("@export_subgroup")) {
							head->add_member_group(annotation);
							// This annotation must appear after script-level annotations and `class_name`/`extends`,
							// so we stop looking for script-level stuff.
							return false;
						} else if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
							// Some annotations need to be resolved and applied in the parser.
							annotation->apply(this, nullptr, nullptr);
						} else {
							push_error(R"(Unexpected standalone annotation.)");
						}
					} else {
						annotation_stack.push_back(annotation);
						// This annotation must appear after script-level annotations and `class_name`/`extends`,
						// so we stop looking for script-level stuff.
						return false;
					}
				}
			} else if (check(GDScriptTokenizer::Token::LITERAL) && current.literal.get_type() == Variant::STRING) {
				// Allow strings in class body as multiline comments.
				advance();
				if (!match(GDScriptTokenizer::Token::NEWLINE)) {
					push_error("Expected newline after comment string.");
				}
			} else {
				break;
			}
		}

		return true;
	};

	bool can_have_class_or_extends = parse_top_level_annotations(AnnotationInfo::SCRIPT | AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);

	if (current.type == GDScriptTokenizer::Token::NAMESPACE || current.type == GDScriptTokenizer::Token::IMPORT || current.type == GDScriptTokenizer::Token::CLASS_NAME || current.type == GDScriptTokenizer::Token::TRAIT_NAME || current.type == GDScriptTokenizer::Token::EXTENDS || current.type == GDScriptTokenizer::Token::USES) {
		// Set range of the class to only start at the top-level declaration if present.
		reset_extents(head, current);
	}

	if ((current.type == GDScriptTokenizer::Token::NAMESPACE || current.type == GDScriptTokenizer::Token::IMPORT) && !annotation_stack.is_empty()) {
		bool was_in_panic_mode = panic_mode;
		push_error(R"(Class annotations must appear after "namespace" and "import" declarations.)");
		annotation_stack.clear();
		panic_mode = was_in_panic_mode;
	}

	bool can_have_namespace_or_import = can_have_class_or_extends;
	while (can_have_namespace_or_import) {
		switch (current.type) {
			case GDScriptTokenizer::Token::NAMESPACE:
				advance();
				parse_namespace();
				break;
			case GDScriptTokenizer::Token::IMPORT:
				advance();
				parse_import();
				break;
			case GDScriptTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(GDScriptTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				can_have_namespace_or_import = false;
				break;
		}

		if (panic_mode) {
			synchronize();
		}
	}

	if (can_have_class_or_extends) {
		can_have_class_or_extends = parse_top_level_annotations(AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);
	}

	while (can_have_class_or_extends) {
		// Order here doesn't matter, but there should be only one of each at most.
		switch (current.type) {
			case GDScriptTokenizer::Token::FINAL: {
				// A top-level `final` marks the whole-file head class final, but only when it
				// immediately precedes the head keyword it applies to: `class_name` or `extends`.
				// Otherwise it is an ordinary declaration modifier on the first body member; leave it
				// unconsumed so `parse_class_body()` validates it through the shared collector.
				const GDScriptTokenizer::Token::Type next_type = peek().type;
				if (next_type == GDScriptTokenizer::Token::TRAIT_NAME) {
					// A trait is meant to be mixed in, so it cannot be final. Reject it here rather
					// than marking the head final, mirroring how inline `final trait` is rejected.
					advance();
					push_error(R"(The "final" modifier cannot be applied to traits.)");
					break;
				}
				if (next_type != GDScriptTokenizer::Token::CLASS_NAME &&
						next_type != GDScriptTokenizer::Token::EXTENDS) {
					can_have_class_or_extends = false;
					break;
				}
				advance();
				if (head->is_final) {
					push_error(R"(The "final" modifier was already specified.)");
				} else if (head->is_abstract) {
					push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
				} else {
					head->is_final = true;
				}
			} break;
			case GDScriptTokenizer::Token::ABSTRACT: {
				// A top-level `abstract` marks the whole-file head class abstract, but only when it
				// immediately precedes the head keyword it applies to: `class_name`, `trait_name`, or
				// `extends`. A file with neither `class_name` nor `extends` has no head declaration to
				// mark abstract, so it must add an explicit `extends` (for example
				// `abstract extends RefCounted`) to be abstract.
				//
				// When `abstract` is followed by anything else (`var`, `func`, `class`, `trait`, ...)
				// it is an ordinary declaration modifier on the first body member; leave it unconsumed
				// so `parse_class_body()` validates it through the shared modifier collector.
				const GDScriptTokenizer::Token::Type next_type = peek().type;
				if (next_type != GDScriptTokenizer::Token::CLASS_NAME &&
						next_type != GDScriptTokenizer::Token::TRAIT_NAME &&
						next_type != GDScriptTokenizer::Token::EXTENDS) {
					can_have_class_or_extends = false;
					break;
				}
				advance();
				if (head->is_abstract) {
					push_error(R"(The "abstract" modifier was already specified.)");
				} else if (head->is_final) {
					push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
				} else {
					head->is_abstract = true;
				}
			} break;
			case GDScriptTokenizer::Token::CLASS_NAME:
				push_pending_annotations_to_head();
				advance();
				if (head->trait_name_used) {
					push_error(R"("class_name" cannot be combined with "trait_name" in the same file.)");
				} else if (head->identifier != nullptr) {
					push_error(R"("class_name" can only be used once.)");
				} else {
					parse_class_name();
				}
				break;
			case GDScriptTokenizer::Token::TRAIT_NAME:
				push_pending_annotations_to_head();
				advance();
				if (head->trait_name_used) {
					push_error(R"("trait_name" can only be used once.)");
				} else if (head->identifier != nullptr) {
					push_error(R"("trait_name" cannot be combined with "class_name" in the same file.)");
				} else {
					parse_trait_name();
				}
				break;
			case GDScriptTokenizer::Token::EXTENDS:
				push_pending_annotations_to_head();
				advance();
				if (head->uses_used) {
					push_error(R"("extends" must appear before "uses".)");
				} else if (head->extends_used) {
					push_error(R"("extends" can only be used once.)");
				} else {
					parse_extends();
					if (match(GDScriptTokenizer::Token::USES)) {
						parse_uses();
						end_statement("uses declaration");
					} else {
						end_statement("superclass");
					}
				}
				break;
			case GDScriptTokenizer::Token::USES:
				push_pending_annotations_to_head();
				advance();
				parse_uses();
				end_statement("uses declaration");
				break;
			case GDScriptTokenizer::Token::TK_EOF:
				push_pending_annotations_to_head();
				can_have_class_or_extends = false;
				break;
			case GDScriptTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(GDScriptTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				// No tokens are allowed between script annotations and class/extends.
				can_have_class_or_extends = false;
				break;
		}

		if (panic_mode) {
			synchronize();
		}
	}

	// When the only thing needed is the class name, icon, and abstractness; we don't need to parse the whole file.
	// It really speed up the call to `GDScriptLanguage::get_global_class_name()` especially for large script.
	if (!parse_body) {
		return;
	}

	parse_class_body(true);

	head->end_line = current.end_line;
	head->end_column = current.end_column;

	complete_extents(head);

#ifdef TOOLS_ENABLED
	const HashMap<int, GDScriptTokenizer::CommentData> &comments = tokenizer->get_comments();

	int max_line = head->end_line;
	if (!head->members.is_empty()) {
		max_line = MIN(max_line, head->members[0].get_line() - 1);
	}
	// `max_script_doc_line` is lowered past any doc comment already consumed by a member or an
	// annotation declaration. Clamp by it so those doc comments are never reused as the class
	// description, including in scripts whose only declarations are annotations (no members).
	max_line = MIN(max_line, max_script_doc_line);

	int line = 0;
	while (line <= max_line) {
		// Find the start.
		if (comments.has(line) && comments[line].new_line && comments[line].comment.begins_with("##")) {
			// Find the end.
			while (line + 1 <= max_line && comments.has(line + 1) && comments[line + 1].new_line && comments[line + 1].comment.begins_with("##")) {
				line++;
			}
			head->doc_data = parse_class_doc_comment(line);
			break;
		}
		line++;
	}
#endif // TOOLS_ENABLED

	if (!check(GDScriptTokenizer::Token::TK_EOF)) {
		push_error("Expected end of file.");
	}

	clear_unused_annotations();
}

Ref<GDScriptParserRef> GDScriptParser::get_depended_parser_for(const String &p_path) {
	Ref<GDScriptParserRef> ref;
	if (depended_parsers.has(p_path)) {
		ref = depended_parsers[p_path];
	} else {
		Error err = OK;
		ref = GDScriptCache::get_parser(p_path, GDScriptParserRef::EMPTY, err, script_path);
		if (ref.is_valid()) {
			depended_parsers[p_path] = ref;
		}
	}

	return ref;
}

const HashMap<String, Ref<GDScriptParserRef>> &GDScriptParser::get_depended_parsers() {
	return depended_parsers;
}

GDScriptParser::ClassNode *GDScriptParser::find_class(const String &p_qualified_name) const {
	String first = p_qualified_name.get_slice("::", 0);

	Vector<String> class_names;
	GDScriptParser::ClassNode *result = nullptr;
	// Empty initial name means start at the head.
	if (first.is_empty() || (head->identifier && first == head->identifier->name)) {
		class_names = p_qualified_name.split("::");
		result = head;
	} else if (p_qualified_name.begins_with(script_path)) {
		// Script path could have a class path separator("::") in it.
		class_names = p_qualified_name.trim_prefix(script_path).split("::");
		result = head;
	} else if (head->has_member(first)) {
		class_names = p_qualified_name.split("::");
		GDScriptParser::ClassNode::Member member = head->get_member(first);
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			result = member.m_class;
		}
	}

	// Starts at index 1 because index 0 was handled above.
	for (int i = 1; result != nullptr && i < class_names.size(); i++) {
		const String &current_name = class_names[i];
		GDScriptParser::ClassNode *next = nullptr;
		if (result->has_member(current_name)) {
			GDScriptParser::ClassNode::Member member = result->get_member(current_name);
			if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
				next = member.m_class;
			}
		}
		result = next;
	}

	return result;
}

bool GDScriptParser::has_class(const GDScriptParser::ClassNode *p_class) const {
	if (head->fqcn.is_empty() && p_class->fqcn.get_slice("::", 0).is_empty()) {
		return p_class == head;
	} else if (p_class->fqcn.begins_with(head->fqcn)) {
		return find_class(p_class->fqcn.trim_prefix(head->fqcn)) == p_class;
	}

	return false;
}

bool GDScriptParser::parse_identifier_chain(const String &p_declaration_name, String &r_chain) {
	if (p_declaration_name == "import") {
		make_completion_context(COMPLETION_IMPORT_NAMESPACE, nullptr);
	}
	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, vformat(R"(Expected identifier after "%s".)", p_declaration_name))) {
		return false;
	}

	r_chain = parse_identifier()->name;

	while (match(GDScriptTokenizer::Token::PERIOD)) {
		if (p_declaration_name == "import") {
			make_completion_context(COMPLETION_IMPORT_NAMESPACE, nullptr);
		}
		if (!consume(GDScriptTokenizer::Token::IDENTIFIER, vformat(R"(Expected identifier after "." in %s declaration.)", p_declaration_name))) {
			return false;
		}
		r_chain += "." + String(parse_identifier()->name);
	}

	return true;
}

void GDScriptParser::parse_namespace() {
	bool can_store_namespace = true;
	if (!head->namespace_name.is_empty()) {
		push_error(R"("namespace" can only be used once.)");
		can_store_namespace = false;
	} else if (!head->imports.is_empty()) {
		push_error(R"("namespace" must be declared before "import".)");
		can_store_namespace = false;
	}

	String namespace_name;
	if (parse_identifier_chain("namespace", namespace_name) && can_store_namespace) {
		head->namespace_name = namespace_name;
	}

	if (!panic_mode) {
		end_statement("namespace declaration");
	}
}

void GDScriptParser::parse_import() {
	String import;
	if (parse_identifier_chain("import", import)) {
		head->imports.push_back(import);
	}

	if (!panic_mode) {
		end_statement("import declaration");
	}
}

GDScriptParser::ClassNode *GDScriptParser::parse_class(const DeclarationModifiers &p_modifiers) {
	ClassNode *n_class = alloc_node<ClassNode>();

	ClassNode *previous_class = current_class;
	current_class = n_class;
	n_class->outer = previous_class;
	n_class->is_abstract = p_modifiers.is_abstract;
	n_class->is_final = p_modifiers.is_final;

	if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier for the class name after "class".)")) {
		n_class->identifier = parse_identifier();
		if (n_class->outer) {
			String fqcn = n_class->outer->fqcn;
			if (fqcn.is_empty()) {
				fqcn = GDScript::canonicalize_path(script_path);
			}
			n_class->fqcn = fqcn + "::" + n_class->identifier->name;
		} else {
			n_class->fqcn = n_class->identifier->name;
		}
	}

	parse_type_parameters(n_class->type_parameters);

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		parse_extends();
	}

	if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
	}

	consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after class declaration.)");

	bool multiline = match(GDScriptTokenizer::Token::NEWLINE);

	if (multiline && !consume(GDScriptTokenizer::Token::INDENT, R"(Expected indented block after class declaration.)")) {
		current_class = previous_class;
		complete_extents(n_class);
		return n_class;
	}

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		if (n_class->extends_used) {
			push_error(R"(Cannot use "extends" more than once in the same class.)");
		}
		parse_extends();
		if (match(GDScriptTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("superclass");
		}
	} else if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	}

	parse_class_body(multiline);
	complete_extents(n_class);

	if (multiline) {
		consume(GDScriptTokenizer::Token::DEDENT, R"(Missing unindent at the end of the class body.)");
	}

	current_class = previous_class;
	return n_class;
}

GDScriptParser::TraitNode *GDScriptParser::parse_trait(const DeclarationModifiers &p_modifiers) {
	TraitNode *trait = alloc_node<TraitNode>();

	ClassNode *previous_class = current_class;
	current_class = trait;
	trait->outer = previous_class;
	trait->is_abstract = p_modifiers.is_abstract;

	if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier for the trait name after "trait".)")) {
		trait->identifier = parse_identifier();
		if (trait->outer) {
			String fqcn = trait->outer->fqcn;
			if (fqcn.is_empty()) {
				fqcn = GDScript::canonicalize_path(script_path);
			}
			trait->fqcn = fqcn + "::" + trait->identifier->name;
		} else {
			trait->fqcn = trait->identifier->name;
		}
	}

	// A trait may be generic (`trait Container[T]`), mirroring `class Box[T]`.
	parse_type_parameters(trait->type_parameters);

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		parse_extends();
	}

	if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
	}

	consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after trait declaration.)");

	bool multiline = match(GDScriptTokenizer::Token::NEWLINE);

	if (multiline && !consume(GDScriptTokenizer::Token::INDENT, R"(Expected indented block after trait declaration.)")) {
		current_class = previous_class;
		complete_extents(trait);
		return trait;
	}

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		if (trait->extends_used) {
			push_error(R"(Cannot use "extends" more than once in the same trait.)");
		}
		parse_extends();
		if (match(GDScriptTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("supertrait");
		}
	} else if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	}

	parse_class_body(multiline);
	complete_extents(trait);

	if (multiline) {
		consume(GDScriptTokenizer::Token::DEDENT, R"(Missing unindent at the end of the trait body.)");
	}

	current_class = previous_class;
	return trait;
}

void GDScriptParser::parse_class_name() {
	if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier for the global class name after "class_name".)")) {
		current_class->identifier = parse_identifier();
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}

	parse_type_parameters(current_class->type_parameters);

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		// Allow extends on the same line.
		parse_extends();
		if (match(GDScriptTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("superclass");
		}
	} else if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	} else {
		end_statement("class_name statement");
	}
}

void GDScriptParser::parse_trait_name() {
	if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier for the global trait name after "trait_name".)")) {
		current_class->is_trait = true;
		current_class->trait_name_used = true;
		current_class->identifier = parse_identifier();
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}

	// A global trait may be generic (`trait_name Container[T]`), mirroring `class_name Box[T]`.
	parse_type_parameters(current_class->type_parameters);

	if (match(GDScriptTokenizer::Token::EXTENDS)) {
		// Allow extends on the same line.
		parse_extends();
		if (match(GDScriptTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("supertrait");
		}
	} else if (match(GDScriptTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	} else {
		end_statement("trait_name statement");
	}
}

void GDScriptParser::parse_extends() {
	current_class->extends_used = true;

	int chain_index = 0;

	// Type arguments specializing a generic base: `extends List[T]`, `extends List[int]`, or on a
	// path-based base, `extends "res://base.gd"[int]`.
	auto parse_extends_type_arguments = [this]() {
		if (match(GDScriptTokenizer::Token::BRACKET_OPEN)) {
			if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Expected at least one type argument after "[".)");
			} else {
				do {
					TypeNode *type_argument = parse_type();
					if (type_argument == nullptr) {
						push_error(R"(Expected type argument after "[".)");
						break;
					}
					current_class->extends_type_arguments.push_back(type_argument);
				} while (match(GDScriptTokenizer::Token::COMMA));
			}
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type arguments.)");
		}
	};

	if (match(GDScriptTokenizer::Token::LITERAL)) {
		if (previous.literal.get_type() != Variant::STRING) {
			push_error(vformat(R"(Only strings or identifiers can be used after "extends", found "%s" instead.)", Variant::get_type_name(previous.literal.get_type())));
		}
		current_class->extends_path = previous.literal;

		if (!match(GDScriptTokenizer::Token::PERIOD)) {
			// A path-based base with no `.Inner` chain can still carry type arguments.
			parse_extends_type_arguments();
			return;
		}
	}

	make_completion_context(COMPLETION_INHERIT_TYPE, current_class, chain_index++);

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected superclass name after "extends".)")) {
		return;
	}
	current_class->extends.push_back(parse_identifier());

	while (match(GDScriptTokenizer::Token::PERIOD)) {
		make_completion_context(COMPLETION_INHERIT_TYPE, current_class, chain_index++);
		if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected superclass name after ".".)")) {
			return;
		}
		current_class->extends.push_back(parse_identifier());
	}

	parse_extends_type_arguments();
}

void GDScriptParser::parse_uses() {
	if (current_class->uses_used) {
		push_error(vformat(R"(Cannot use "uses" more than once in the same %s.)", current_class->is_trait ? "trait" : "class"));
	}
	current_class->uses_used = true;

	do {
		ClassNode::TraitUse trait_use;
		int chain_index = 0;
		make_completion_context(COMPLETION_USES, current_class, chain_index++, true, &trait_use.name);
		if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected trait name after "uses".)")) {
			return;
		}
		trait_use.name.push_back(parse_identifier());

		while (match(GDScriptTokenizer::Token::PERIOD)) {
			make_completion_context(COMPLETION_USES, current_class, chain_index++, true, &trait_use.name);
			if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected trait name after ".".)")) {
				return;
			}
			trait_use.name.push_back(parse_identifier());
		}

		// Type arguments specializing a generic trait: `uses Container[int]`.
		if (match(GDScriptTokenizer::Token::BRACKET_OPEN)) {
			if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Expected at least one type argument after "[".)");
			} else {
				do {
					TypeNode *type_argument = parse_type();
					if (type_argument == nullptr) {
						push_error(R"(Expected type argument after "[".)");
						break;
					}
					trait_use.type_arguments.push_back(type_argument);
				} while (match(GDScriptTokenizer::Token::COMMA));
			}
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type arguments.)");
		}

		current_class->used_traits.push_back(trait_use);
	} while (match(GDScriptTokenizer::Token::COMMA));
}

void GDScriptParser::parse_type_parameters(Vector<TypeParameterNode *> &r_type_parameters) {
	if (!match(GDScriptTokenizer::Token::BRACKET_OPEN)) {
		return;
	}

	if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
		push_error(R"(Expected type parameter name after "[".)");
	} else {
		do {
			if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			TypeParameterNode *type_parameter = alloc_node<TypeParameterNode>();
			if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected type parameter name.)")) {
				type_parameter->identifier = parse_identifier();
				for (const TypeParameterNode *previous_parameter : r_type_parameters) {
					if (previous_parameter->identifier != nullptr && previous_parameter->identifier->name == type_parameter->identifier->name) {
						push_error(vformat(R"(Type parameter with name "%s" was already declared.)", type_parameter->identifier->name), type_parameter->identifier);
						break;
					}
				}
				if (match(GDScriptTokenizer::Token::COLON)) {
					type_parameter->bound = parse_type();
					if (type_parameter->bound == nullptr) {
						push_error(R"(Expected type parameter bound after ":".)");
					}
				}
			}
			complete_extents(type_parameter);
			r_type_parameters.push_back(type_parameter);
		} while (match(GDScriptTokenizer::Token::COMMA));
	}

	consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type parameters.)");
}

List<GDScriptParser::AnnotationNode *> GDScriptParser::parse_class_member_annotations(AnnotationInfo::TargetKind p_target, const String &p_member_kind) {
	List<AnnotationNode *> annotations;
	while (!annotation_stack.is_empty()) {
		AnnotationNode *last_annotation = annotation_stack.back()->get();
		if (last_annotation->applies_to(p_target)) {
			annotations.push_front(last_annotation);
			annotation_stack.pop_back();
		} else {
			push_error(vformat(R"(Annotation "%s" cannot be applied to a %s.)", last_annotation->name, p_member_kind));
			clear_unused_annotations();
		}
	}
	return annotations;
}

template <typename T>
void GDScriptParser::finalize_class_member(T *p_member, List<AnnotationNode *> &p_annotations, const String &p_member_kind) {
	if (p_member == nullptr) {
		return;
	}

#ifdef TOOLS_ENABLED
	int doc_comment_line = p_member->start_line - 1;
#endif // TOOLS_ENABLED

	for (AnnotationNode *&annotation : p_annotations) {
		p_member->annotations.push_back(annotation);
#ifdef TOOLS_ENABLED
		if (annotation->start_line <= doc_comment_line) {
			doc_comment_line = annotation->start_line - 1;
		}
#endif // TOOLS_ENABLED
	}

#ifdef TOOLS_ENABLED
	if constexpr (std::is_base_of_v<ClassNode, T>) {
		if (has_comment(p_member->start_line, true)) {
			// Inline doc comment.
			p_member->doc_data = parse_class_doc_comment(p_member->start_line, true);
		} else if (has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment. Don't check `min_member_doc_line` because a class ends parsing after its members.
			// This may not work correctly for cases like `var a; class B`, but it doesn't matter in practice.
			p_member->doc_data = parse_class_doc_comment(doc_comment_line);
		}
	} else {
		if (has_comment(p_member->start_line, true)) {
			// Inline doc comment.
			p_member->doc_data = parse_doc_comment(p_member->start_line, true);
		} else if (doc_comment_line >= min_member_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			p_member->doc_data = parse_doc_comment(doc_comment_line);
		}
	}

	min_member_doc_line = p_member->end_line + 1; // Prevent multiple members from using the same doc comment.
#endif // TOOLS_ENABLED

	if (p_member->identifier != nullptr) {
		if (!((String)p_member->identifier->name).is_empty()) { // Enums may be unnamed.
			if (current_class->members_indices.has(p_member->identifier->name)) {
				push_error(vformat(R"(%s "%s" has the same name as a previously declared %s.)", p_member_kind.capitalize(), p_member->identifier->name, current_class->get_member(p_member->identifier->name).get_type_name()), p_member->identifier);
			} else {
				current_class->add_member(p_member);
			}
		} else {
			current_class->add_member(p_member);
		}
	}
}

template <typename T>
void GDScriptParser::parse_class_member(T *(GDScriptParser::*p_parse_function)(const DeclarationModifiers &), AnnotationInfo::TargetKind p_target, const String &p_member_kind, const DeclarationModifiers &p_modifiers) {
	advance();
	List<AnnotationNode *> annotations = parse_class_member_annotations(p_target, p_member_kind);
	T *member = (this->*p_parse_function)(p_modifiers);
	finalize_class_member(member, annotations, p_member_kind);
}

void GDScriptParser::parse_function_class_member(const DeclarationModifiers &p_modifiers) {
	advance();
	List<AnnotationNode *> annotations = parse_class_member_annotations(AnnotationInfo::FUNCTION, "function");
	FunctionNode *member = parse_function_declaration(p_modifiers);
	finalize_class_member(member, annotations, "function");
}

GDScriptParser::AnnotationDeclarationNode *GDScriptParser::parse_annotation_declaration() {
	AnnotationDeclarationNode *annotation_declaration = alloc_node<AnnotationDeclarationNode>();

	// The current token is the contextual `annotation` identifier. `alloc_node` anchored the
	// node to the preceding token, so re-anchor it to `annotation` itself once consumed; this
	// keeps analyzer diagnostics (duplicate/reserved identity) pointing at the declaration.
	advance();
	reset_extents(annotation_declaration, previous);

	// An annotation declaration is not a runtime member, so no class-level annotation may
	// apply to it. Consume any pending annotations here (erroring on each) so they cannot
	// silently carry over onto the next real member.
	parse_class_member_annotations(AnnotationInfo::NONE, "annotation declaration");

	// Annotation declarations are root-only in v1. Inner classes, traits, functions, and
	// local scopes never reach a valid declaration here.
	const bool is_root_declaration = current_class->outer == nullptr && !current_class->is_trait;
	if (!is_root_declaration) {
		push_error(R"(Annotation declarations are only allowed at the root of a script.)");
	}

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected annotation name after "annotation".)")) {
		complete_extents(annotation_declaration);
		return nullptr;
	}
	annotation_declaration->identifier = parse_identifier();

	if (match(GDScriptTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		parse_annotation_declaration_parameters(annotation_declaration);
		pop_multiline();
		consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after annotation parameters.)*");
	}

	if (current.type == GDScriptTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("targets")) {
		advance();
		parse_annotation_declaration_targets(annotation_declaration);
	} else {
		push_error(R"(Expected "targets" after the annotation name.)");
	}

	complete_extents(annotation_declaration);
	end_statement("annotation declaration");

	// Canonical identity uses the root namespace, which is parsed before the body.
	const String &namespace_name = head != nullptr ? head->namespace_name : String();
	annotation_declaration->qualified_name = namespace_name.is_empty()
			? String(annotation_declaration->identifier->name)
			: namespace_name + "." + String(annotation_declaration->identifier->name);

#ifdef TOOLS_ENABLED
	// Capture the doc comment so generated docs can describe the declaration. Annotation
	// declarations never carry leading `@annotations`, so the comment is on the line right
	// before the declaration (or inline on the same line).
	int doc_comment_line = annotation_declaration->start_line - 1;
	if (has_comment(annotation_declaration->start_line, true)) {
		// Inline doc comment.
		annotation_declaration->doc_data = parse_doc_comment(annotation_declaration->start_line, true);
	} else if (doc_comment_line >= min_member_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
		// Normal doc comment.
		annotation_declaration->doc_data = parse_doc_comment(doc_comment_line);
	}
	min_member_doc_line = annotation_declaration->end_line + 1; // Prevent reuse of the same doc comment.
#endif // TOOLS_ENABLED

	if (is_root_declaration) {
		current_class->annotation_declarations.push_back(annotation_declaration);
	}

	return annotation_declaration;
}

void GDScriptParser::parse_annotation_declaration_parameters(AnnotationDeclarationNode *p_annotation_declaration) {
	if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE) || is_at_end()) {
		return;
	}

	bool default_used = false;
	do {
		if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
			break; // Allow for trailing comma.
		}

		bool is_rest = false;
		if (match(GDScriptTokenizer::Token::PERIOD_PERIOD_PERIOD)) {
			is_rest = true;
		}

		ParameterNode *parameter = parse_parameter();
		if (parameter == nullptr) {
			break;
		}

		if (p_annotation_declaration->rest_parameter != nullptr) {
			push_error("Cannot have parameters after the variadic parameter.");
			continue;
		}

		if (parameter->initializer != nullptr) {
			if (is_rest) {
				push_error("The variadic parameter cannot have a default value.");
				continue;
			}
			default_used = true;
		} else if (default_used && !is_rest) {
			push_error("Cannot have mandatory parameters after optional parameters.");
			continue;
		}

		if (p_annotation_declaration->parameters_indices.has(parameter->identifier->name)) {
			push_error(vformat(R"(Parameter with name "%s" was already declared for this annotation.)", parameter->identifier->name));
		} else if (is_rest) {
			p_annotation_declaration->rest_parameter = parameter;
		} else {
			p_annotation_declaration->parameters_indices[parameter->identifier->name] = p_annotation_declaration->parameters.size();
			p_annotation_declaration->parameters.push_back(parameter);
		}
	} while (match(GDScriptTokenizer::Token::COMMA));
}

void GDScriptParser::parse_annotation_declaration_targets(AnnotationDeclarationNode *p_annotation_declaration) {
	do {
		uint32_t target_bit = AnnotationDeclarationNode::TARGET_NONE;
		String target_name;

		// Target names (`CLASS`, `METHOD`, `VARIABLE`, `SIGNAL`, `CONSTANT`) are uppercase, so they
		// arrive as ordinary identifiers rather than the lowercase keyword tokens.
		if (match(GDScriptTokenizer::Token::IDENTIFIER)) {
			target_name = previous.get_identifier();
			if (target_name == "CLASS") {
				target_bit = AnnotationDeclarationNode::TARGET_CLASS;
			} else if (target_name == "METHOD") {
				target_bit = AnnotationDeclarationNode::TARGET_METHOD;
			} else if (target_name == "VARIABLE") {
				target_bit = AnnotationDeclarationNode::TARGET_VARIABLE;
			} else if (target_name == "SIGNAL") {
				target_bit = AnnotationDeclarationNode::TARGET_SIGNAL;
			} else if (target_name == "CONSTANT") {
				target_bit = AnnotationDeclarationNode::TARGET_CONSTANT;
			} else {
				push_error(vformat(R"(Unknown annotation target "%s". Expected "CLASS", "METHOD", "VARIABLE", "SIGNAL", or "CONSTANT".)", target_name));
			}
		} else {
			push_error(R"(Expected an annotation target name.)");
			break;
		}

		if (target_bit != AnnotationDeclarationNode::TARGET_NONE) {
			if (p_annotation_declaration->targets & target_bit) {
				push_error(vformat(R"(Annotation target "%s" was already declared.)", target_name));
			} else {
				p_annotation_declaration->targets |= target_bit;
			}
		}
	} while (match(GDScriptTokenizer::Token::COMMA));
}

GDScriptParser::DeclarationModifiers GDScriptParser::collect_declaration_modifiers() {
	DeclarationModifiers modifiers;
	while (true) {
		if (check(GDScriptTokenizer::Token::FINAL)) {
			advance();
			if (modifiers.is_final) {
				push_error(R"(The "final" modifier was already specified.)");
			}
			modifiers.is_final = true;
			modifiers.final_line = previous.start_line;
			modifiers.final_column = previous.start_column;
		} else if (check(GDScriptTokenizer::Token::ABSTRACT)) {
			advance();
			if (modifiers.is_abstract) {
				push_error(R"(The "abstract" modifier was already specified.)");
			}
			modifiers.is_abstract = true;
			modifiers.abstract_line = previous.start_line;
			modifiers.abstract_column = previous.start_column;
		} else if (check(GDScriptTokenizer::Token::STATIC)) {
			advance();
			if (modifiers.is_static) {
				push_error(R"(The "static" modifier was already specified.)");
			}
			modifiers.is_static = true;
			modifiers.static_line = previous.start_line;
			modifiers.static_column = previous.start_column;
		} else if (current.type == GDScriptTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("async")) {
			advance();
			if (modifiers.is_async) {
				push_error(R"(The "async" modifier was already specified.)");
			}
			modifiers.is_async = true;
			modifiers.async_line = previous.start_line;
			modifiers.async_column = previous.start_column;
		} else {
			break;
		}
	}
	return modifiers;
}

void GDScriptParser::validate_declaration_modifiers(const DeclarationModifiers &p_modifiers, const char *p_target_kind, bool p_allow_abstract, bool p_allow_static, bool p_allow_async, bool p_allow_final, bool p_in_trait) {
	if (p_modifiers.is_final && !p_allow_final) {
		push_error(vformat(R"(The "final" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_abstract && !p_allow_abstract) {
		push_error(vformat(R"(The "abstract" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_static && !p_allow_static) {
		push_error(vformat(R"(The "static" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_async && !p_allow_async) {
		push_error(vformat(R"(The "async" modifier cannot be applied to %s.)", p_target_kind));
	}
	// Combination rules only apply where each modifier is individually valid.
	if (p_allow_abstract && p_allow_static && p_modifiers.is_abstract && p_modifiers.is_static && !p_in_trait) {
		push_error(R"(The "abstract" and "static" modifiers cannot be combined outside a trait.)");
	}
	// `final` and `abstract` are contradictory on both classes and methods: `final` forbids
	// extension/override, `abstract` requires it.
	if (p_allow_final && p_allow_abstract && p_modifiers.is_final && p_modifiers.is_abstract) {
		push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
	}
	// `abstract async` is intentionally allowed: it declares an async contract that
	// forces overriding implementations to be async. `final static` is also allowed.
}

void GDScriptParser::parse_class_body(bool p_is_multiline) {
	bool class_end = false;
	while (!class_end && !is_at_end()) {
		DeclarationModifiers modifiers = collect_declaration_modifiers();
		const bool in_trait = current_class != nullptr && current_class->is_trait;

		GDScriptTokenizer::Token token = current;
		const bool starts_declaration = token.type == GDScriptTokenizer::Token::VAR ||
				token.type == GDScriptTokenizer::Token::TK_CONST ||
				token.type == GDScriptTokenizer::Token::SIGNAL ||
				token.type == GDScriptTokenizer::Token::FUNC ||
				token.type == GDScriptTokenizer::Token::CLASS ||
				token.type == GDScriptTokenizer::Token::TRAIT ||
				token.type == GDScriptTokenizer::Token::ENUM;
		if (modifiers.has_any() && !starts_declaration) {
			push_error(R"(Expected a declaration after the modifier.)");
		}

		switch (token.type) {
			case GDScriptTokenizer::Token::VAR:
				validate_declaration_modifiers(modifiers, "variables", false, true, false, true, in_trait);
				parse_class_member(&GDScriptParser::parse_variable, AnnotationInfo::VARIABLE, "variable", modifiers);
				if (modifiers.is_static) {
					current_class->has_static_data = true;
				}
				break;
			case GDScriptTokenizer::Token::TK_CONST:
				validate_declaration_modifiers(modifiers, "constants", false, false, false, false, in_trait);
				parse_class_member(&GDScriptParser::parse_constant, AnnotationInfo::CONSTANT, "constant", modifiers);
				break;
			case GDScriptTokenizer::Token::SIGNAL:
				validate_declaration_modifiers(modifiers, "signals", false, false, false, false, in_trait);
				parse_class_member(&GDScriptParser::parse_signal, AnnotationInfo::SIGNAL, "signal", modifiers);
				break;
			case GDScriptTokenizer::Token::FUNC:
				validate_declaration_modifiers(modifiers, "functions", true, true, true, true, in_trait);
				parse_function_class_member(modifiers);
				break;
			case GDScriptTokenizer::Token::CLASS:
				validate_declaration_modifiers(modifiers, "classes", true, false, false, true, in_trait);
				parse_class_member(&GDScriptParser::parse_class, AnnotationInfo::CLASS, "class", modifiers);
				break;
			case GDScriptTokenizer::Token::TRAIT:
				validate_declaration_modifiers(modifiers, "traits", true, false, false, false, in_trait);
				parse_class_member(&GDScriptParser::parse_trait, AnnotationInfo::CLASS, "trait", modifiers);
				break;
			case GDScriptTokenizer::Token::ENUM:
				validate_declaration_modifiers(modifiers, "enums", false, false, false, false, in_trait);
				parse_class_member(&GDScriptParser::parse_enum, AnnotationInfo::NONE, "enum", modifiers);
				break;
			case GDScriptTokenizer::Token::ANNOTATION: {
				advance();

				// Check for class-level and standalone annotations.
				AnnotationNode *annotation = parse_annotation(AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);
				if (annotation != nullptr) {
					if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
						if (previous.type != GDScriptTokenizer::Token::NEWLINE) {
							push_error(R"(Expected newline after a standalone annotation.)");
						}
						if (annotation->name == SNAME("@export_category") || annotation->name == SNAME("@export_group") || annotation->name == SNAME("@export_subgroup")) {
							current_class->add_member_group(annotation);
						} else if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
							// Some annotations need to be resolved and applied in the parser.
							annotation->apply(this, nullptr, nullptr);
						} else {
							push_error(R"(Unexpected standalone annotation.)");
						}
					} else { // `AnnotationInfo::CLASS_LEVEL`.
						annotation_stack.push_back(annotation);
					}
				}
				break;
			}
			case GDScriptTokenizer::Token::PASS:
				advance();
				end_statement(R"("pass")");
				break;
			case GDScriptTokenizer::Token::NAMESPACE:
				advance();
				push_error(R"("namespace" declarations must appear before "import", "class_name", "extends", and body declarations.)");
				break;
			case GDScriptTokenizer::Token::IMPORT:
				advance();
				push_error(R"("import" declarations must appear before "class_name", "extends", and body declarations.)");
				break;
			case GDScriptTokenizer::Token::TRAIT_NAME:
				advance();
				push_error(R"("trait_name" declarations must appear before class body declarations.)");
				break;
			case GDScriptTokenizer::Token::EXTENDS:
				advance();
				if (current_class->uses_used) {
					push_error(R"("extends" must appear before "uses".)");
				} else {
					push_error(vformat(R"(Unexpected %s in class body.)", previous.get_debug_name()));
				}
				break;
			case GDScriptTokenizer::Token::USES:
				advance();
				if (current_class->uses_used) {
					push_error(vformat(R"(Cannot use "uses" more than once in the same %s.)", current_class->is_trait ? "trait" : "class"));
				} else {
					push_error(R"("uses" declarations must appear before class body declarations.)");
				}
				break;
			case GDScriptTokenizer::Token::DEDENT:
				class_end = true;
				break;
			case GDScriptTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(GDScriptTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				if (token.type == GDScriptTokenizer::Token::IDENTIFIER && token.get_identifier() == StringName("annotation")) {
					// `annotation` is contextual: it only starts a declaration where a root-body
					// declaration is valid. Anywhere else it remains an ordinary identifier.
					parse_annotation_declaration();
					break;
				}
				// Display a completion with declaration-oriented identifiers.
				make_completion_context(COMPLETION_DECLARATION, nullptr);
				advance();
				if (previous.get_identifier() == "export") {
					push_error(R"(The "export" keyword was removed in Godot 4. Use an export annotation ("@export", "@export_range", etc.) instead.)");
				} else if (previous.get_identifier() == "tool") {
					push_error(R"(The "tool" keyword was removed in Godot 4. Use the "@tool" annotation instead.)");
				} else if (previous.get_identifier() == "onready") {
					push_error(R"(The "onready" keyword was removed in Godot 4. Use the "@onready" annotation instead.)");
				} else if (previous.get_identifier() == "remote") {
					push_error(R"(The "remote" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" instead.)");
				} else if (previous.get_identifier() == "remotesync") {
					push_error(R"(The "remotesync" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and "call_local" instead.)");
				} else if (previous.get_identifier() == "puppet") {
					push_error(R"(The "puppet" keyword was removed in Godot 4. Use the "@rpc" annotation with "authority" instead.)");
				} else if (previous.get_identifier() == "puppetsync") {
					push_error(R"(The "puppetsync" keyword was removed in Godot 4. Use the "@rpc" annotation with "authority" and "call_local" instead.)");
				} else if (previous.get_identifier() == "master") {
					push_error(R"(The "master" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and perform a check inside the function instead.)");
				} else if (previous.get_identifier() == "mastersync") {
					push_error(R"(The "mastersync" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and "call_local", and perform a check inside the function instead.)");
				} else {
					push_error(vformat(R"(Unexpected %s in class body.)", previous.get_debug_name()));
				}
				break;
		}
		if (panic_mode) {
			synchronize();
		}
		if (!p_is_multiline) {
			class_end = true;
		}
	}
}

GDScriptParser::VariableNode *GDScriptParser::parse_variable(const DeclarationModifiers &p_modifiers) {
	return parse_variable(p_modifiers.is_static, true, p_modifiers.is_final);
}

GDScriptParser::VariableNode *GDScriptParser::parse_variable(bool p_is_static, bool p_allow_property, bool p_is_final) {
	VariableNode *variable = alloc_node<VariableNode>();

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected variable name after "var".)")) {
		complete_extents(variable);
		return nullptr;
	}

	variable->identifier = parse_identifier();
	variable->export_info.name = variable->identifier->name;
	variable->is_static = p_is_static;
	variable->is_final = p_is_final;

	if (match(GDScriptTokenizer::Token::COLON)) {
		if (p_allow_property) {
			make_completion_context(COMPLETION_PROPERTY_DECLARATION_OR_TYPE, variable);
		}
		if (check(GDScriptTokenizer::Token::NEWLINE)) {
			if (p_allow_property) {
				advance();
				return parse_property(variable, true);
			} else {
				push_error(R"(Expected type after ":")");
				complete_extents(variable);
				return nullptr;
			}
		} else if (check((GDScriptTokenizer::Token::EQUAL))) {
			// Infer type.
			variable->infer_datatype = true;
		} else {
			if (p_allow_property) {
				if (check(GDScriptTokenizer::Token::IDENTIFIER)) {
					// Check if get or set.
					if (current.get_identifier() == "get" || current.get_identifier() == "set") {
						return parse_property(variable, false);
					}
				}
			}

			// Parse type.
			variable->datatype_specifier = parse_type();
		}
	}

	if (match(GDScriptTokenizer::Token::EQUAL)) {
		// Initializer.
		variable->initializer = parse_expression(false);
		if (variable->initializer == nullptr) {
			push_error(R"(Expected expression for variable initial value after "=".)");
		}
		variable->assignments++;
	}

	if (p_allow_property && match(GDScriptTokenizer::Token::COLON)) {
		if (match(GDScriptTokenizer::Token::NEWLINE)) {
			return parse_property(variable, true);
		} else {
			return parse_property(variable, false);
		}
	}

	complete_extents(variable);
	end_statement("variable declaration");

	return variable;
}

GDScriptParser::VariableNode *GDScriptParser::parse_property(VariableNode *p_variable, bool p_need_indent) {
	if (p_need_indent) {
		if (!consume(GDScriptTokenizer::Token::INDENT, R"(Expected indented block for property after ":".)")) {
			complete_extents(p_variable);
			return nullptr;
		}
	}

	VariableNode *property = p_variable;

	make_completion_context(COMPLETION_PROPERTY_DECLARATION, property);

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected "get" or "set" for property declaration.)")) {
		complete_extents(p_variable);
		return nullptr;
	}

	IdentifierNode *function = parse_identifier();

	if (check(GDScriptTokenizer::Token::EQUAL)) {
		p_variable->property = VariableNode::PROP_SETGET;
	} else {
		p_variable->property = VariableNode::PROP_INLINE;
		if (!p_need_indent) {
			push_error("Property with inline code must go to an indented block.");
		}
	}

	bool getter_used = false;
	bool setter_used = false;

	// Run with a loop because order doesn't matter.
	for (int i = 0; i < 2; i++) {
		if (function->name == SNAME("set")) {
			if (setter_used) {
				push_error(R"(Properties can only have one setter.)");
			} else {
				parse_property_setter(property);
				setter_used = true;
			}
		} else if (function->name == SNAME("get")) {
			if (getter_used) {
				push_error(R"(Properties can only have one getter.)");
			} else {
				parse_property_getter(property);
				getter_used = true;
			}
		} else {
			if (setter_used && !getter_used) {
				push_error(R"(Expected "get" for property declaration.)");
			} else if (getter_used && !setter_used) {
				push_error(R"(Expected "set" for property declaration.)");
			} else {
				push_error(R"(Expected "get" or "set" for property declaration.)");
			}
		}

		if (i == 0 && p_variable->property == VariableNode::PROP_SETGET) {
			if (match(GDScriptTokenizer::Token::COMMA)) {
				// Consume potential newline.
				if (match(GDScriptTokenizer::Token::NEWLINE)) {
					if (!p_need_indent) {
						push_error(R"(Inline setter/getter setting cannot span across multiple lines (use "\\"" if needed).)");
					}
				}
			} else {
				break;
			}
		}

		if (!match(GDScriptTokenizer::Token::IDENTIFIER)) {
			break;
		}
		function = parse_identifier();
	}
	complete_extents(p_variable);

	if (p_variable->property == VariableNode::PROP_SETGET) {
		end_statement("property declaration");
	}

	if (p_need_indent) {
		consume(GDScriptTokenizer::Token::DEDENT, R"(Expected end of indented block for property.)");
	}
	return property;
}

void GDScriptParser::parse_property_setter(VariableNode *p_variable) {
	switch (p_variable->property) {
		case VariableNode::PROP_INLINE: {
			FunctionNode *function = alloc_node<FunctionNode>();
			IdentifierNode *identifier = alloc_node<IdentifierNode>();
			complete_extents(identifier);
			identifier->name = "@" + p_variable->identifier->name + "_setter";
			function->identifier = identifier;
			function->is_static = p_variable->is_static;

			consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "set".)");

			ParameterNode *parameter = alloc_node<ParameterNode>();
			if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected parameter name after "(".)")) {
				reset_extents(parameter, previous);
				p_variable->setter_parameter = parse_identifier();
				parameter->identifier = p_variable->setter_parameter;
				function->parameters_indices[parameter->identifier->name] = 0;
				function->parameters.push_back(parameter);
			}
			complete_extents(parameter);

			consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after parameter name.)*");
			consume(GDScriptTokenizer::Token::COLON, R"*(Expected ":" after ")".)*");

			FunctionNode *previous_function = current_function;
			current_function = function;
			if (p_variable->setter_parameter != nullptr) {
				SuiteNode *body = alloc_node<SuiteNode>();
				body->add_local(parameter, function);
				function->body = parse_suite("setter declaration", body);
				p_variable->setter = function;
			}
			current_function = previous_function;
			complete_extents(function);
			break;
		}
		case VariableNode::PROP_SETGET:
			consume(GDScriptTokenizer::Token::EQUAL, R"(Expected "=" after "set")");
			make_completion_context(COMPLETION_PROPERTY_METHOD, p_variable);
			if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected setter function name after "=".)")) {
				p_variable->setter_pointer = parse_identifier();
			}
			break;
		case VariableNode::PROP_NONE:
			break; // Unreachable.
	}
}

void GDScriptParser::parse_property_getter(VariableNode *p_variable) {
	switch (p_variable->property) {
		case VariableNode::PROP_INLINE: {
			FunctionNode *function = alloc_node<FunctionNode>();

			if (match(GDScriptTokenizer::Token::PARENTHESIS_OPEN)) {
				consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after "get(".)*");
				consume(GDScriptTokenizer::Token::COLON, R"*(Expected ":" after "get()".)*");
			} else {
				consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" or "(" after "get".)");
			}

			IdentifierNode *identifier = alloc_node<IdentifierNode>();
			complete_extents(identifier);
			identifier->name = "@" + p_variable->identifier->name + "_getter";
			function->identifier = identifier;
			function->is_static = p_variable->is_static;

			FunctionNode *previous_function = current_function;
			current_function = function;

			SuiteNode *body = alloc_node<SuiteNode>();
			function->body = parse_suite("getter declaration", body);
			p_variable->getter = function;

			current_function = previous_function;
			complete_extents(function);
			break;
		}
		case VariableNode::PROP_SETGET:
			consume(GDScriptTokenizer::Token::EQUAL, R"(Expected "=" after "get")");
			make_completion_context(COMPLETION_PROPERTY_METHOD, p_variable);
			if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected getter function name after "=".)")) {
				p_variable->getter_pointer = parse_identifier();
			}
			break;
		case VariableNode::PROP_NONE:
			break; // Unreachable.
	}
}

GDScriptParser::ConstantNode *GDScriptParser::parse_constant(const DeclarationModifiers &p_modifiers) {
	ConstantNode *constant = alloc_node<ConstantNode>();

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected constant name after "const".)")) {
		complete_extents(constant);
		return nullptr;
	}

	constant->identifier = parse_identifier();

	if (match(GDScriptTokenizer::Token::COLON)) {
		if (check((GDScriptTokenizer::Token::EQUAL))) {
			// Infer type.
			constant->infer_datatype = true;
		} else {
			// Parse type.
			constant->datatype_specifier = parse_type();
		}
	}

	if (consume(GDScriptTokenizer::Token::EQUAL, R"(Expected initializer after constant name.)")) {
		// Initializer.
		constant->initializer = parse_expression(false);

		if (constant->initializer == nullptr) {
			push_error(R"(Expected initializer expression for constant.)");
			complete_extents(constant);
			return nullptr;
		}
	} else {
		complete_extents(constant);
		return nullptr;
	}

	complete_extents(constant);
	end_statement("constant declaration");

	return constant;
}

GDScriptParser::ParameterNode *GDScriptParser::parse_parameter() {
	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected parameter name.)")) {
		return nullptr;
	}

	ParameterNode *parameter = alloc_node<ParameterNode>();
	parameter->identifier = parse_identifier();

	if (match(GDScriptTokenizer::Token::COLON)) {
		if (check((GDScriptTokenizer::Token::EQUAL))) {
			// Infer type.
			parameter->infer_datatype = true;
		} else {
			// Parse type.
			make_completion_context(COMPLETION_TYPE_NAME, parameter);
			parameter->datatype_specifier = parse_type();
		}
	}

	if (match(GDScriptTokenizer::Token::EQUAL)) {
		// Default value.
		parameter->initializer = parse_expression(false);
	}

	complete_extents(parameter);
	return parameter;
}

GDScriptParser::SignalNode *GDScriptParser::parse_signal(const DeclarationModifiers &p_modifiers) {
	SignalNode *signal = alloc_node<SignalNode>();

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected signal name after "signal".)")) {
		complete_extents(signal);
		return nullptr;
	}

	signal->identifier = parse_identifier();

	if (check(GDScriptTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		advance();
		do {
			if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ParameterNode *parameter = parse_parameter();
			if (parameter == nullptr) {
				push_error("Expected signal parameter name.");
				break;
			}
			if (parameter->initializer != nullptr) {
				push_error(R"(Signal parameters cannot have a default value.)");
			}
			if (signal->parameters_indices.has(parameter->identifier->name)) {
				push_error(vformat(R"(Parameter with name "%s" was already declared for this signal.)", parameter->identifier->name));
			} else {
				signal->parameters_indices[parameter->identifier->name] = signal->parameters.size();
				signal->parameters.push_back(parameter);
			}
		} while (match(GDScriptTokenizer::Token::COMMA) && !is_at_end());

		pop_multiline();
		consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after signal parameters.)*");
	}

	complete_extents(signal);
	end_statement("signal declaration");

	return signal;
}

GDScriptParser::EnumNode *GDScriptParser::parse_enum(const DeclarationModifiers &p_modifiers) {
	EnumNode *enum_node = alloc_node<EnumNode>();
	bool named = false;

	if (match(GDScriptTokenizer::Token::IDENTIFIER)) {
		enum_node->identifier = parse_identifier();
		named = true;
	}

	push_multiline(true);
	consume(GDScriptTokenizer::Token::BRACE_OPEN, vformat(R"(Expected "{" after %s.)", named ? "enum name" : R"("enum")"));
#ifdef TOOLS_ENABLED
	int min_enum_value_doc_line = previous.end_line + 1;
#endif

	HashMap<StringName, int> elements;

#ifdef DEBUG_ENABLED
	List<MethodInfo> gdscript_funcs;
	GDScriptLanguage::get_singleton()->get_public_functions(&gdscript_funcs);
#endif

	do {
		if (check(GDScriptTokenizer::Token::BRACE_CLOSE)) {
			break; // Allow trailing comma.
		}
		if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier for enum key.)")) {
			GDScriptParser::IdentifierNode *identifier = parse_identifier();

			EnumNode::Value item;
			item.identifier = identifier;
			item.parent_enum = enum_node;
			item.line = previous.start_line;
			item.start_column = previous.start_column;
			item.end_column = previous.end_column;

			if (elements.has(item.identifier->name)) {
				push_error(vformat(R"(Name "%s" was already in this enum (at line %d).)", item.identifier->name, elements[item.identifier->name]), item.identifier);
			} else if (!named) {
				if (current_class->members_indices.has(item.identifier->name)) {
					push_error(vformat(R"(Name "%s" is already used as a class %s.)", item.identifier->name, current_class->get_member(item.identifier->name).get_type_name()));
				}
			}

			elements[item.identifier->name] = item.line;

			if (match(GDScriptTokenizer::Token::EQUAL)) {
				ExpressionNode *value = parse_expression(false);
				if (value == nullptr) {
					push_error(R"(Expected expression value after "=".)");
				}
				item.custom_value = value;
			}

			item.index = enum_node->values.size();
			enum_node->values.push_back(item);
			if (!named) {
				// Add as member of current class.
				current_class->add_member(item);
			}
		}
	} while (match(GDScriptTokenizer::Token::COMMA));

#ifdef TOOLS_ENABLED
	// Enum values documentation.
	for (int i = 0; i < enum_node->values.size(); i++) {
		int enum_value_line = enum_node->values[i].line;
		int doc_comment_line = enum_value_line - 1;

		MemberDocData doc_data;
		if (has_comment(enum_value_line, true)) {
			// Inline doc comment.
			if (i == enum_node->values.size() - 1 || enum_node->values[i + 1].line > enum_value_line) {
				doc_data = parse_doc_comment(enum_value_line, true);
			}
		} else if (doc_comment_line >= min_enum_value_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			doc_data = parse_doc_comment(doc_comment_line);
		}

		if (named) {
			enum_node->values.write[i].doc_data = doc_data;
		} else {
			current_class->set_enum_value_doc_data(enum_node->values[i].identifier->name, doc_data);
		}

		min_enum_value_doc_line = enum_value_line + 1; // Prevent multiple enum values from using the same doc comment.
	}
#endif // TOOLS_ENABLED

	pop_multiline();
	consume(GDScriptTokenizer::Token::BRACE_CLOSE, R"(Expected closing "}" for enum.)");
	complete_extents(enum_node);
	end_statement("enum");

	return enum_node;
}

bool GDScriptParser::parse_function_signature(FunctionNode *p_function, SuiteNode *p_body, const String &p_type, int p_signature_start) {
	if (!check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE) && !is_at_end()) {
		bool default_used = false;
		do {
			if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			bool is_rest = false;
			if (match(GDScriptTokenizer::Token::PERIOD_PERIOD_PERIOD)) {
				is_rest = true;
			}

			ParameterNode *parameter = parse_parameter();
			if (parameter == nullptr) {
				break;
			}

			if (p_function->is_vararg()) {
				push_error("Cannot have parameters after the rest parameter.");
				continue;
			}

			if (parameter->initializer != nullptr) {
				if (is_rest) {
					push_error("The rest parameter cannot have a default value.");
					continue;
				}
				default_used = true;
			} else {
				if (default_used && !is_rest) {
					push_error("Cannot have mandatory parameters after optional parameters.");
					continue;
				}
			}

			if (p_function->parameters_indices.has(parameter->identifier->name)) {
				push_error(vformat(R"(Parameter with name "%s" was already declared for this %s.)", parameter->identifier->name, p_type));
			} else if (is_rest) {
				p_function->rest_parameter = parameter;
				p_body->add_local(parameter, current_function);
			} else {
				p_function->parameters_indices[parameter->identifier->name] = p_function->parameters.size();
				p_function->parameters.push_back(parameter);
				p_body->add_local(parameter, current_function);
			}
		} while (match(GDScriptTokenizer::Token::COMMA));
	}

	pop_multiline();
	consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, vformat(R"*(Expected closing ")" after %s parameters.)*", p_type));

	if (match(GDScriptTokenizer::Token::FORWARD_ARROW)) {
		make_completion_context(COMPLETION_TYPE_NAME_OR_VOID, p_function);
		p_function->return_type = parse_type(true);
		if (p_function->return_type == nullptr) {
			push_error(R"(Expected return type or "void" after "->".)");
		}
	}

	if (!p_function->source_lambda && p_function->identifier && p_function->identifier->name == GDScriptLanguage::get_singleton()->strings._static_init) {
		if (!p_function->is_static) {
			push_error(R"(Static constructor must be declared static.)");
		}
		if (!p_function->parameters.is_empty() || p_function->is_vararg()) {
			push_error(R"(Static constructor cannot have parameters.)");
		}
		current_class->has_static_data = true;
	}

#ifdef TOOLS_ENABLED
	if (p_type == "function" && p_signature_start != -1) {
		const int signature_end_pos = tokenizer->get_current_position() - 1;
		const String source_code = tokenizer->get_source_code();
		p_function->signature = source_code.substr(p_signature_start, signature_end_pos - p_signature_start).strip_edges(false, true);
	}
#endif // TOOLS_ENABLED

	// TODO: Improve token consumption so it synchronizes to a statement boundary. This way we can get into the function body with unrecognized tokens.
	if (p_type == "lambda") {
		return consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after lambda declaration.)");
	}
	// The colon may not be present in the case of abstract functions.
	return match(GDScriptTokenizer::Token::COLON);
}

GDScriptParser::FunctionNode *GDScriptParser::parse_function_declaration(const DeclarationModifiers &p_modifiers) {
	FunctionNode *function = alloc_node<FunctionNode>();
	function->is_static = p_modifiers.is_static;
	function->is_abstract = p_modifiers.is_abstract;
	function->is_final = p_modifiers.is_final;
	function->is_declared_async = p_modifiers.is_async;
	// Declared async functions are coroutine-callable even before parsing a body-level await.
	function->is_coroutine = p_modifiers.is_async;

	make_completion_context(COMPLETION_OVERRIDE_METHOD, function);

#ifdef TOOLS_ENABLED
	// The signature is something like `(a: int, b: int = 0) -> void`.
	// We start one token earlier, since the parser looks one token ahead.
	const int signature_start_pos = tokenizer->get_current_position();
#endif // TOOLS_ENABLED

	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected function name after "func".)")) {
		complete_extents(function);
		return nullptr;
	}

	FunctionNode *previous_function = current_function;
	current_function = function;

	function->identifier = parse_identifier();
	if (function->identifier != nullptr && function->identifier->name == SNAME("async") && check(GDScriptTokenizer::Token::IDENTIFIER)) {
		push_error(R"("async" must appear before "func" when used as a function modifier.)", function->identifier);
		advance();
	}

	parse_type_parameters(function->type_parameters);

	SuiteNode *body = alloc_node<SuiteNode>();

	SuiteNode *previous_suite = current_suite;
	current_suite = body;

	push_multiline(true);
	consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after function name.)");

#ifdef TOOLS_ENABLED
	const bool has_body = parse_function_signature(function, body, "function", signature_start_pos);
#else // !TOOLS_ENABLED
	const bool has_body = parse_function_signature(function, body, "function", -1);
#endif // TOOLS_ENABLED

	current_suite = previous_suite;

#ifdef TOOLS_ENABLED
	function->min_local_doc_line = previous.end_line + 1;
#endif // TOOLS_ENABLED

	if (!has_body) {
		// Abstract functions do not have a body.
		end_statement("bodyless function declaration");
		reset_extents(body, current);
		complete_extents(body);
		function->body = body;
	} else {
		function->body = parse_suite("function declaration", body);
	}

	current_function = previous_function;
	complete_extents(function);
	return function;
}

GDScriptParser::AnnotationNode *GDScriptParser::parse_annotation(uint32_t p_valid_targets) {
	AnnotationNode *annotation = alloc_node<AnnotationNode>();

	annotation->name = previous.literal;

	make_completion_context(COMPLETION_ANNOTATION, annotation);

	bool valid = true;

	if (!valid_annotations.has(annotation->name)) {
		if (annotation->name == "@deprecated") {
			push_error(R"("@deprecated" annotation does not exist. Use "## @deprecated: Reason here." instead.)");
			valid = false;
		} else if (annotation->name == "@experimental") {
			push_error(R"("@experimental" annotation does not exist. Use "## @experimental: Reason here." instead.)");
			valid = false;
		} else if (annotation->name == "@tutorial") {
			push_error(R"("@tutorial" annotation does not exist. Use "## @tutorial(Title): https://example.com" instead.)");
			valid = false;
		} else {
			// Unknown non-built-in annotation. Preserve it as an unresolved custom usage so the
			// analyzer can resolve it against same-namespace or imported annotation declarations.
			// Its `info` stays null and its target/arguments are validated later by the analyzer.
			annotation->is_custom = true;
		}
	}

	if (valid && !annotation->is_custom) {
		annotation->info = &valid_annotations[annotation->name];

		if (!annotation->applies_to(p_valid_targets)) {
			if (annotation->applies_to(AnnotationInfo::SCRIPT)) {
				push_error(vformat(R"(Annotation "%s" must be at the top of the script, before "extends" and "class_name".)", annotation->name));
			} else {
				push_error(vformat(R"(Annotation "%s" is not allowed in this level.)", annotation->name));
			}
			valid = false;
		}
	}

	if (check(GDScriptTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		advance();
		// Arguments.
		push_completion_call(annotation);
		int argument_index = 0;
		do {
			make_completion_context(COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
			set_last_completion_call_arg(argument_index);
			if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ExpressionNode *argument = nullptr;
			StringName argument_name;
			const bool accepts_named_arguments = annotation->is_custom || annotation->name == SNAME("@autoload");
			if (accepts_named_arguments) {
				// Custom annotation usages and the built-in `@autoload` annotation accept
				// `name = value` named arguments. Stop on a trailing "=" so a leading
				// identifier can be read as the argument name.
				// GDScript assignment is a statement, never an expression, so `IDENTIFIER` +
				// `EQUAL` here is unambiguously a named argument; `@a(x == y)` uses `EQUAL_EQUAL`.
				argument = parse_expression(false, true);
				if (argument != nullptr && check(GDScriptTokenizer::Token::EQUAL)) {
					if (argument->type == Node::IDENTIFIER) {
						argument_name = static_cast<IdentifierNode *>(argument)->name;
						advance(); // Consume "=".
						make_completion_context(COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
						argument = parse_expression(false);
						if (argument == nullptr) {
							push_error(vformat(R"(Expected expression after "%s =" named argument.)", argument_name));
						}
					} else {
						// A non-identifier target before "=" is an attempted assignment, which is
						// not a valid expression argument. Consume the rest so parsing recovers.
						push_error(R"(Assignment is not allowed inside an expression.)");
						advance(); // Consume "=".
						parse_expression(false);
					}
				}
			} else {
				argument = parse_expression(false);
			}

			if (argument == nullptr) {
				if (argument_name == StringName()) {
					push_error("Expected expression as the annotation argument.");
					valid = false;
				}
			} else {
				annotation->arguments.push_back(argument);
				annotation->argument_names.push_back(argument_name);

				if (argument->type == Node::LITERAL) {
					override_completion_context(argument, COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
				}
			}

			argument_index++;
		} while (match(GDScriptTokenizer::Token::COMMA));

		pop_multiline();
		consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after annotation arguments.)*");
		pop_completion_call();
	}
	complete_extents(annotation);

	match(GDScriptTokenizer::Token::NEWLINE); // Newline after annotation is optional.

	if (valid && !annotation->is_custom) {
		valid = validate_annotation_arguments(annotation);
	}

	return valid ? annotation : nullptr;
}

void GDScriptParser::clear_unused_annotations() {
	for (const AnnotationNode *annotation : annotation_stack) {
		push_error(vformat(R"(Annotation "%s" does not precede a valid target, so it will have no effect.)", annotation->name), annotation);
	}

	annotation_stack.clear();
}

bool GDScriptParser::register_annotation(const MethodInfo &p_info, uint32_t p_target_kinds, AnnotationAction p_apply, const Vector<Variant> &p_default_arguments, bool p_is_vararg) {
	ERR_FAIL_COND_V_MSG(valid_annotations.has(p_info.name), false, vformat(R"(Annotation "%s" already registered.)", p_info.name));

	AnnotationInfo new_annotation;
	new_annotation.info = p_info;
	new_annotation.info.default_arguments = p_default_arguments;
	if (p_is_vararg) {
		new_annotation.info.flags |= METHOD_FLAG_VARARG;
	}
	new_annotation.apply = p_apply;
	new_annotation.target_kind = p_target_kinds;

	valid_annotations[p_info.name] = new_annotation;
	return true;
}

GDScriptParser::SuiteNode *GDScriptParser::parse_suite(const String &p_context, SuiteNode *p_suite, bool p_for_lambda) {
	SuiteNode *suite = p_suite != nullptr ? p_suite : alloc_node<SuiteNode>();
	suite->parent_block = current_suite;
	suite->parent_function = current_function;
	current_suite = suite;

	if (!p_for_lambda && suite->parent_block != nullptr && suite->parent_block->is_in_loop) {
		// Do not reset to false if true is set before calling parse_suite().
		suite->is_in_loop = true;
	}

	bool multiline = false;

	if (match(GDScriptTokenizer::Token::NEWLINE)) {
		multiline = true;
	}

	if (multiline) {
		if (!consume(GDScriptTokenizer::Token::INDENT, vformat(R"(Expected indented block after %s.)", p_context))) {
			current_suite = suite->parent_block;
			complete_extents(suite);
			return suite;
		}
	}
	reset_extents(suite, current);

	int error_count = 0;

	do {
		if (is_at_end() || (!multiline && previous.type == GDScriptTokenizer::Token::SEMICOLON && check(GDScriptTokenizer::Token::NEWLINE))) {
			break;
		}
		Node *statement = parse_statement();
		if (statement == nullptr) {
			if (error_count++ > 100) {
				push_error("Too many statement errors.", suite);
				break;
			}
			continue;
		}
		suite->statements.push_back(statement);

		// Register locals.
		switch (statement->type) {
			case Node::VARIABLE: {
				VariableNode *variable = static_cast<VariableNode *>(statement);
				const SuiteNode::Local &local = current_suite->get_local(variable->identifier->name);
				if (local.type != SuiteNode::Local::UNDEFINED) {
					push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", local.get_name(), variable->identifier->name), variable->identifier);
				}
				current_suite->add_local(variable, current_function);
				break;
			}
			case Node::CONSTANT: {
				ConstantNode *constant = static_cast<ConstantNode *>(statement);
				const SuiteNode::Local &local = current_suite->get_local(constant->identifier->name);
				if (local.type != SuiteNode::Local::UNDEFINED) {
					String name;
					if (local.type == SuiteNode::Local::CONSTANT) {
						name = "constant";
					} else {
						name = "variable";
					}
					push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", name, constant->identifier->name), constant->identifier);
				}
				current_suite->add_local(constant, current_function);
				break;
			}
			default:
				break;
		}

	} while ((multiline || previous.type == GDScriptTokenizer::Token::SEMICOLON) && !check(GDScriptTokenizer::Token::DEDENT) && !lambda_ended && !is_at_end());

	complete_extents(suite);

	if (multiline) {
		if (!lambda_ended) {
			consume(GDScriptTokenizer::Token::DEDENT, vformat(R"(Missing unindent at the end of %s.)", p_context));

		} else {
			match(GDScriptTokenizer::Token::DEDENT);
		}
	} else if (previous.type == GDScriptTokenizer::Token::SEMICOLON) {
		consume(GDScriptTokenizer::Token::NEWLINE, vformat(R"(Expected newline after ";" at the end of %s.)", p_context));
	}

	if (p_for_lambda) {
		lambda_ended = true;
	}
	current_suite = suite->parent_block;
	return suite;
}

GDScriptParser::Node *GDScriptParser::parse_statement() {
	// Compound statements recurse through parse_suite() back into parse_statement();
	// bound that depth so deeply nested blocks (e.g. chained single-line `if`s) report
	// an error instead of overflowing the native stack.
	RecursionDepthGuard depth_guard(statement_nesting_depth);
	if (unlikely(statement_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Statement nesting is too deep.");
		return nullptr;
	}

	Node *result = nullptr;
#ifdef DEBUG_ENABLED
	bool unreachable = current_suite->has_return && !current_suite->has_unreachable_code;
#endif

	List<AnnotationNode *> annotations;
	if (current.type != GDScriptTokenizer::Token::ANNOTATION) {
		while (!annotation_stack.is_empty()) {
			AnnotationNode *last_annotation = annotation_stack.back()->get();
			if (last_annotation->applies_to(AnnotationInfo::STATEMENT)) {
				annotations.push_front(last_annotation);
				annotation_stack.pop_back();
			} else {
				push_error(vformat(R"(Annotation "%s" cannot be applied to a statement.)", last_annotation->name));
				clear_unused_annotations();
			}
		}
	}

	switch (current.type) {
		case GDScriptTokenizer::Token::PASS:
			advance();
			result = alloc_node<PassNode>();
			complete_extents(result);
			end_statement(R"("pass")");
			break;
		case GDScriptTokenizer::Token::VAR:
			advance();
			result = parse_variable(false, false);
			break;
		case GDScriptTokenizer::Token::FINAL:
			advance();
			if (match(GDScriptTokenizer::Token::VAR)) {
				result = parse_variable(false, false, true);
			} else if (check(GDScriptTokenizer::Token::TK_CONST)) {
				advance();
				push_error(R"(The "final" modifier cannot be applied to constants.)");
				result = parse_constant(DeclarationModifiers());
			} else {
				push_error(R"(Expected "var" after "final".)");
			}
			break;
		case GDScriptTokenizer::Token::TK_CONST:
			advance();
			result = parse_constant(DeclarationModifiers());
			break;
		case GDScriptTokenizer::Token::IF:
			advance();
			result = parse_if();
			break;
		case GDScriptTokenizer::Token::FOR:
			advance();
			result = parse_for();
			break;
		case GDScriptTokenizer::Token::WHILE:
			advance();
			result = parse_while();
			break;
		case GDScriptTokenizer::Token::MATCH:
			advance();
			result = parse_match();
			break;
		case GDScriptTokenizer::Token::BREAK:
			advance();
			result = parse_break();
			break;
		case GDScriptTokenizer::Token::CONTINUE:
			advance();
			result = parse_continue();
			break;
		case GDScriptTokenizer::Token::RETURN: {
			advance();
			ReturnNode *n_return = alloc_node<ReturnNode>();
			if (!is_statement_end()) {
				if (current_function && (current_function->identifier->name == GDScriptLanguage::get_singleton()->strings._init || current_function->identifier->name == GDScriptLanguage::get_singleton()->strings._static_init)) {
					push_error(R"(Constructor cannot return a value.)");
				}
				n_return->return_value = parse_expression(false);
			} else if (in_lambda && !is_statement_end_token()) {
				// Try to parse it anyway as this might not be the statement end in a lambda.
				// If this fails the expression will be nullptr, but that's the same as no return, so it's fine.
				n_return->return_value = parse_expression(false);
			}
			complete_extents(n_return);
			result = n_return;

			current_suite->has_return = true;

			end_statement("return statement");
			break;
		}
		case GDScriptTokenizer::Token::BREAKPOINT:
			advance();
			result = alloc_node<BreakpointNode>();
			complete_extents(result);
			end_statement(R"("breakpoint")");
			break;
		case GDScriptTokenizer::Token::ASSERT:
			advance();
			result = parse_assert();
			break;
		case GDScriptTokenizer::Token::ANNOTATION: {
			advance();
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::STATEMENT | AnnotationInfo::STANDALONE);
			if (annotation != nullptr) {
				if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
					if (previous.type != GDScriptTokenizer::Token::NEWLINE) {
						push_error(R"(Expected newline after a standalone annotation.)");
					}
					if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
						// Some annotations need to be resolved and applied in the parser.
						annotation->apply(this, nullptr, nullptr);
					} else {
						push_error(R"(Unexpected standalone annotation.)");
					}
				} else {
					annotation_stack.push_back(annotation);
				}
			}
			break;
		}
		default: {
			// Expression statement.
			ExpressionNode *expression = parse_expression(true); // Allow assignment here.
			bool has_ended_lambda = false;
			if (expression == nullptr) {
				if (in_lambda) {
					// If it's not a valid expression beginning, it might be the continuation of the outer expression where this lambda is.
					lambda_ended = true;
					has_ended_lambda = true;
				} else {
					advance();
					push_error(vformat(R"(Expected statement, found "%s" instead.)", previous.get_name()));
				}
			} else {
				end_statement("expression");
			}
			lambda_ended = lambda_ended || has_ended_lambda;
			result = expression;

#ifdef DEBUG_ENABLED
			if (expression != nullptr) {
				switch (expression->type) {
					case Node::ASSIGNMENT:
					case Node::AWAIT:
					case Node::CALL:
						// Fine.
						break;
					case Node::PRELOAD:
						// `preload` is a function-like keyword.
						push_warning(expression, GDScriptWarning::RETURN_VALUE_DISCARDED, "preload");
						break;
					case Node::LAMBDA:
						// Standalone lambdas can't be used, so make this an error.
						push_error("Standalone lambdas cannot be accessed. Consider assigning it to a variable.", expression);
						break;
					case Node::LITERAL:
						// Allow strings as multiline comments.
						if (static_cast<GDScriptParser::LiteralNode *>(expression)->value.get_type() != Variant::STRING) {
							push_warning(expression, GDScriptWarning::STANDALONE_EXPRESSION);
						}
						break;
					case Node::TERNARY_OPERATOR:
						push_warning(expression, GDScriptWarning::STANDALONE_TERNARY);
						break;
					default:
						push_warning(expression, GDScriptWarning::STANDALONE_EXPRESSION);
				}
			}
#endif
			break;
		}
	}

#ifdef TOOLS_ENABLED
	int doc_comment_line = 0;
	if (result != nullptr) {
		doc_comment_line = result->start_line - 1;
	}
#endif // TOOLS_ENABLED

	if (result != nullptr && !annotations.is_empty()) {
		for (AnnotationNode *&annotation : annotations) {
			result->annotations.push_back(annotation);
#ifdef TOOLS_ENABLED
			if (annotation->start_line <= doc_comment_line) {
				doc_comment_line = annotation->start_line - 1;
			}
#endif // TOOLS_ENABLED
		}
	}

#ifdef TOOLS_ENABLED
	if (result != nullptr) {
		MemberDocData doc_data;
		if (has_comment(result->start_line, true)) {
			// Inline doc comment.
			doc_data = parse_doc_comment(result->start_line, true);
		} else if (doc_comment_line >= current_function->min_local_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			doc_data = parse_doc_comment(doc_comment_line);
		}

		if (result->type == Node::CONSTANT) {
			static_cast<ConstantNode *>(result)->doc_data = doc_data;
		} else if (result->type == Node::VARIABLE) {
			static_cast<VariableNode *>(result)->doc_data = doc_data;
		}

		current_function->min_local_doc_line = result->end_line + 1; // Prevent multiple locals from using the same doc comment.
	}
#endif // TOOLS_ENABLED

#ifdef DEBUG_ENABLED
	if (unreachable && result != nullptr) {
		current_suite->has_unreachable_code = true;
		if (current_function) {
			push_warning(result, GDScriptWarning::UNREACHABLE_CODE, current_function->identifier ? current_function->identifier->name : "<anonymous lambda>");
		}
	}
#endif

	if (panic_mode) {
		synchronize();
	}

	return result;
}

GDScriptParser::AssertNode *GDScriptParser::parse_assert() {
	// TODO: Add assert message.
	AssertNode *assert = alloc_node<AssertNode>();

	push_multiline(true);
	consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "assert".)");

	assert->condition = parse_expression(false);
	if (assert->condition == nullptr) {
		push_error("Expected expression to assert.");
		pop_multiline();
		complete_extents(assert);
		return nullptr;
	}

	if (match(GDScriptTokenizer::Token::COMMA) && !check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
		assert->message = parse_expression(false);
		if (assert->message == nullptr) {
			push_error(R"(Expected error message for assert after ",".)");
			pop_multiline();
			complete_extents(assert);
			return nullptr;
		}
		match(GDScriptTokenizer::Token::COMMA);
	}

	pop_multiline();
	consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after assert expression.)*");

	complete_extents(assert);
	end_statement(R"("assert")");

	return assert;
}

GDScriptParser::BreakNode *GDScriptParser::parse_break() {
	if (!can_break) {
		push_error(R"(Cannot use "break" outside of a loop.)");
	}
	BreakNode *break_node = alloc_node<BreakNode>();
	complete_extents(break_node);
	end_statement(R"("break")");
	return break_node;
}

GDScriptParser::ContinueNode *GDScriptParser::parse_continue() {
	if (!can_continue) {
		push_error(R"(Cannot use "continue" outside of a loop.)");
	}
	current_suite->has_continue = true;
	ContinueNode *cont = alloc_node<ContinueNode>();
	complete_extents(cont);
	end_statement(R"("continue")");
	return cont;
}

GDScriptParser::ForNode *GDScriptParser::parse_for() {
	ForNode *n_for = alloc_node<ForNode>();

	if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected loop variable name after "for".)")) {
		n_for->variable = parse_identifier();
	}

	if (match(GDScriptTokenizer::Token::COLON)) {
		n_for->datatype_specifier = parse_type();
		if (n_for->datatype_specifier == nullptr) {
			push_error(R"(Expected type specifier after ":".)");
		}
	}

	if (n_for->datatype_specifier == nullptr) {
		consume(GDScriptTokenizer::Token::TK_IN, R"(Expected "in" or ":" after "for" variable name.)");
	} else {
		consume(GDScriptTokenizer::Token::TK_IN, R"(Expected "in" after "for" variable type specifier.)");
	}

	n_for->list = parse_expression(false);

	if (!n_for->list) {
		push_error(R"(Expected iterable after "in".)");
	}

	consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after "for" condition.)");

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Allow break/continue.
	can_break = true;
	can_continue = true;

	SuiteNode *suite = alloc_node<SuiteNode>();
	if (n_for->variable) {
		const SuiteNode::Local &local = current_suite->get_local(n_for->variable->name);
		if (local.type != SuiteNode::Local::UNDEFINED) {
			push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", local.get_name(), n_for->variable->name), n_for->variable);
		}
		suite->add_local(SuiteNode::Local(n_for->variable, current_function));
	}
	suite->is_in_loop = true;
	n_for->loop = parse_suite(R"("for" block)", suite);
	complete_extents(n_for);

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return n_for;
}

GDScriptParser::IfNode *GDScriptParser::parse_if(const String &p_token) {
	// `elif` chains recurse directly through parse_if() (bypassing parse_statement()), so
	// bound them with the statement depth counter to avoid overflowing the native stack.
	RecursionDepthGuard depth_guard(statement_nesting_depth);
	if (unlikely(statement_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Statement nesting is too deep.");
		return nullptr;
	}

	IfNode *n_if = alloc_node<IfNode>();

	n_if->condition = parse_expression(false);
	if (n_if->condition == nullptr) {
		push_error(vformat(R"(Expected conditional expression after "%s".)", p_token));
	}

	consume(GDScriptTokenizer::Token::COLON, vformat(R"(Expected ":" after "%s" condition.)", p_token));

	n_if->true_block = parse_suite(vformat(R"("%s" block)", p_token));
	n_if->true_block->parent_if = n_if;

	if (n_if->true_block->has_continue) {
		current_suite->has_continue = true;
	}

	if (match(GDScriptTokenizer::Token::ELIF)) {
		SuiteNode *else_block = alloc_node<SuiteNode>();
		else_block->parent_function = current_function;
		else_block->parent_block = current_suite;

		SuiteNode *previous_suite = current_suite;
		current_suite = else_block;

		IfNode *elif = parse_if("elif");
		if (elif != nullptr) {
			else_block->statements.push_back(elif);
		}
		complete_extents(else_block);
		n_if->false_block = else_block;

		current_suite = previous_suite;
	} else if (match(GDScriptTokenizer::Token::ELSE)) {
		consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after "else".)");
		n_if->false_block = parse_suite(R"("else" block)");
	}
	complete_extents(n_if);

	if (n_if->false_block != nullptr && n_if->false_block->has_return && n_if->true_block->has_return) {
		current_suite->has_return = true;
	}
	if (n_if->false_block != nullptr && n_if->false_block->has_continue) {
		current_suite->has_continue = true;
	}

	return n_if;
}

GDScriptParser::MatchNode *GDScriptParser::parse_match() {
	MatchNode *match_node = alloc_node<MatchNode>();

	match_node->test = parse_expression(false);
	if (match_node->test == nullptr) {
		push_error(R"(Expected expression to test after "match".)");
	}

	consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after "match" expression.)");
	consume(GDScriptTokenizer::Token::NEWLINE, R"(Expected a newline after "match" statement.)");

	if (!consume(GDScriptTokenizer::Token::INDENT, R"(Expected an indented block after "match" statement.)")) {
		complete_extents(match_node);
		return match_node;
	}

	bool all_have_return = true;
	bool have_wildcard = false;

	List<AnnotationNode *> match_branch_annotation_stack;

	while (!check(GDScriptTokenizer::Token::DEDENT) && !is_at_end()) {
		if (match(GDScriptTokenizer::Token::PASS)) {
			consume(GDScriptTokenizer::Token::NEWLINE, R"(Expected newline after "pass".)");
			continue;
		}

		if (match(GDScriptTokenizer::Token::ANNOTATION)) {
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::STATEMENT);
			if (annotation == nullptr) {
				continue;
			}
			if (annotation->name != SNAME("@warning_ignore")) {
				push_error(vformat(R"(Annotation "%s" is not allowed in this level.)", annotation->name), annotation);
				continue;
			}
			match_branch_annotation_stack.push_back(annotation);
			continue;
		}

		MatchBranchNode *branch = parse_match_branch();
		if (branch == nullptr) {
			advance();
			continue;
		}

		for (AnnotationNode *annotation : match_branch_annotation_stack) {
			branch->annotations.push_back(annotation);
		}
		match_branch_annotation_stack.clear();

#ifdef DEBUG_ENABLED
		if (have_wildcard && !branch->patterns.is_empty()) {
			push_warning(branch->patterns[0], GDScriptWarning::UNREACHABLE_PATTERN);
		}
#endif

		have_wildcard = have_wildcard || branch->has_wildcard;
		all_have_return = all_have_return && branch->block->has_return;
		match_node->branches.push_back(branch);
	}
	complete_extents(match_node);

	consume(GDScriptTokenizer::Token::DEDENT, R"(Expected an indented block after "match" statement.)");

	if (all_have_return && have_wildcard) {
		current_suite->has_return = true;
	}

	for (const AnnotationNode *annotation : match_branch_annotation_stack) {
		push_error(vformat(R"(Annotation "%s" does not precede a valid target, so it will have no effect.)", annotation->name), annotation);
	}
	match_branch_annotation_stack.clear();

	return match_node;
}

GDScriptParser::MatchBranchNode *GDScriptParser::parse_match_branch() {
	MatchBranchNode *branch = alloc_node<MatchBranchNode>();
	reset_extents(branch, current);

	bool has_bind = false;

	do {
		PatternNode *pattern = parse_match_pattern();
		if (pattern == nullptr) {
			continue;
		}
		if (pattern->binds.size() > 0) {
			has_bind = true;
		}
		if (branch->patterns.size() > 0 && has_bind) {
			push_error(R"(Cannot use a variable bind with multiple patterns.)");
		}
		if (pattern->pattern_type == PatternNode::PT_REST) {
			push_error(R"(Rest pattern can only be used inside array and dictionary patterns.)");
		} else if (pattern->pattern_type == PatternNode::PT_BIND || pattern->pattern_type == PatternNode::PT_WILDCARD) {
			branch->has_wildcard = true;
		}
		branch->patterns.push_back(pattern);
	} while (match(GDScriptTokenizer::Token::COMMA));

	if (branch->patterns.is_empty()) {
		push_error(R"(No pattern found for "match" branch.)");
	}

	bool has_guard = false;
	if (match(GDScriptTokenizer::Token::WHEN)) {
		// Pattern guard.
		// Create block for guard because it also needs to access the bound variables from patterns, and we don't want to add them to the outer scope.
		branch->guard_body = alloc_node<SuiteNode>();
		if (branch->patterns.size() > 0) {
			for (const KeyValue<StringName, IdentifierNode *> &E : branch->patterns[0]->binds) {
				SuiteNode::Local local(E.value, current_function);
				local.type = SuiteNode::Local::PATTERN_BIND;
				branch->guard_body->add_local(local);
			}
		}

		SuiteNode *parent_block = current_suite;
		branch->guard_body->parent_block = parent_block;
		current_suite = branch->guard_body;

		ExpressionNode *guard = parse_expression(false);
		if (guard == nullptr) {
			push_error(R"(Expected expression for pattern guard after "when".)");
		} else {
			branch->guard_body->statements.append(guard);
		}
		current_suite = parent_block;
		complete_extents(branch->guard_body);

		has_guard = true;
		branch->has_wildcard = false; // If it has a guard, the wildcard might still not match.
	}

	if (!consume(GDScriptTokenizer::Token::COLON, vformat(R"(Expected ":"%s after "match" %s.)", has_guard ? "" : R"( or "when")", has_guard ? "pattern guard" : "patterns"))) {
		branch->block = alloc_recovery_suite();
		complete_extents(branch);
		// Consume the whole line and treat the next one as new match branch.
		while (current.type != GDScriptTokenizer::Token::NEWLINE && !is_at_end()) {
			advance();
		}
		if (!is_at_end()) {
			advance();
		}
		return branch;
	}

	SuiteNode *suite = alloc_node<SuiteNode>();
	if (branch->patterns.size() > 0) {
		for (const KeyValue<StringName, IdentifierNode *> &E : branch->patterns[0]->binds) {
			SuiteNode::Local local(E.value, current_function);
			local.type = SuiteNode::Local::PATTERN_BIND;
			suite->add_local(local);
		}
	}

	branch->block = parse_suite("match pattern block", suite);
	complete_extents(branch);

	return branch;
}

GDScriptParser::PatternNode *GDScriptParser::parse_match_pattern(PatternNode *p_root_pattern) {
	// Array and dictionary patterns recurse through parse_match_pattern(); bound that
	// depth so deeply nested patterns report an error instead of overflowing the stack.
	RecursionDepthGuard depth_guard(pattern_nesting_depth);
	if (unlikely(pattern_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Pattern nesting is too deep.");
		return nullptr;
	}

	PatternNode *pattern = alloc_node<PatternNode>();
	reset_extents(pattern, current);

	switch (current.type) {
		case GDScriptTokenizer::Token::VAR: {
			// Bind.
			advance();
			if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected bind name after "var".)")) {
				complete_extents(pattern);
				return nullptr;
			}
			pattern->pattern_type = PatternNode::PT_BIND;
			pattern->bind = parse_identifier();

			PatternNode *root_pattern = p_root_pattern == nullptr ? pattern : p_root_pattern;

			if (p_root_pattern != nullptr) {
				if (p_root_pattern->has_bind(pattern->bind->name)) {
					push_error(vformat(R"(Bind variable name "%s" was already used in this pattern.)", pattern->bind->name));
					complete_extents(pattern);
					return nullptr;
				}
			}

			if (current_suite->has_local(pattern->bind->name)) {
				push_error(vformat(R"(There's already a %s named "%s" in this scope.)", current_suite->get_local(pattern->bind->name).get_name(), pattern->bind->name));
				complete_extents(pattern);
				return nullptr;
			}

			root_pattern->binds[pattern->bind->name] = pattern->bind;

		} break;
		case GDScriptTokenizer::Token::UNDERSCORE:
			// Wildcard.
			advance();
			pattern->pattern_type = PatternNode::PT_WILDCARD;
			break;
		case GDScriptTokenizer::Token::PERIOD_PERIOD:
			// Rest.
			advance();
			pattern->pattern_type = PatternNode::PT_REST;
			break;
		case GDScriptTokenizer::Token::BRACKET_OPEN: {
			// Array.
			push_multiline(true);
			advance();
			pattern->pattern_type = PatternNode::PT_ARRAY;
			do {
				if (is_at_end() || check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
					break;
				}
				PatternNode *sub_pattern = parse_match_pattern(p_root_pattern != nullptr ? p_root_pattern : pattern);
				if (sub_pattern == nullptr) {
					continue;
				}
				if (pattern->rest_used) {
					push_error(R"(The ".." pattern must be the last element in the pattern array.)");
				} else if (sub_pattern->pattern_type == PatternNode::PT_REST) {
					pattern->rest_used = true;
				}
				pattern->array.push_back(sub_pattern);
			} while (match(GDScriptTokenizer::Token::COMMA));
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected "]" to close the array pattern.)");
			pop_multiline();
			break;
		}
		case GDScriptTokenizer::Token::BRACE_OPEN: {
			// Dictionary.
			push_multiline(true);
			advance();
			pattern->pattern_type = PatternNode::PT_DICTIONARY;
			do {
				if (check(GDScriptTokenizer::Token::BRACE_CLOSE) || is_at_end()) {
					break;
				}
				if (match(GDScriptTokenizer::Token::PERIOD_PERIOD)) {
					// Rest.
					if (pattern->rest_used) {
						push_error(R"(The ".." pattern must be the last element in the pattern dictionary.)");
					} else {
						PatternNode *sub_pattern = alloc_node<PatternNode>();
						complete_extents(sub_pattern);
						sub_pattern->pattern_type = PatternNode::PT_REST;
						pattern->dictionary.push_back({ nullptr, sub_pattern });
						pattern->rest_used = true;
					}
				} else {
					ExpressionNode *key = parse_expression(false);
					if (key == nullptr) {
						push_error(R"(Expected expression as key for dictionary pattern.)");
					}
					if (match(GDScriptTokenizer::Token::COLON)) {
						// Value pattern.
						PatternNode *sub_pattern = parse_match_pattern(p_root_pattern != nullptr ? p_root_pattern : pattern);
						if (sub_pattern == nullptr) {
							continue;
						}
						if (pattern->rest_used) {
							push_error(R"(The ".." pattern must be the last element in the pattern dictionary.)");
						} else if (sub_pattern->pattern_type == PatternNode::PT_REST) {
							push_error(R"(The ".." pattern cannot be used as a value.)");
						} else {
							pattern->dictionary.push_back({ key, sub_pattern });
						}
					} else {
						// Key match only.
						pattern->dictionary.push_back({ key, nullptr });
					}
				}
			} while (match(GDScriptTokenizer::Token::COMMA));
			consume(GDScriptTokenizer::Token::BRACE_CLOSE, R"(Expected "}" to close the dictionary pattern.)");
			pop_multiline();
			break;
		}
		default: {
			// Expression.
			ExpressionNode *expression = parse_expression(false);
			if (expression == nullptr) {
				push_error(R"(Expected expression for match pattern.)");
				complete_extents(pattern);
				return nullptr;
			} else {
				if (expression->type == GDScriptParser::Node::LITERAL) {
					pattern->pattern_type = PatternNode::PT_LITERAL;
				} else {
					pattern->pattern_type = PatternNode::PT_EXPRESSION;
				}
				pattern->expression = expression;
			}
			break;
		}
	}
	complete_extents(pattern);

	return pattern;
}

bool GDScriptParser::PatternNode::has_bind(const StringName &p_name) {
	return binds.has(p_name);
}

GDScriptParser::IdentifierNode *GDScriptParser::PatternNode::get_bind(const StringName &p_name) {
	return binds[p_name];
}

GDScriptParser::WhileNode *GDScriptParser::parse_while() {
	WhileNode *n_while = alloc_node<WhileNode>();

	n_while->condition = parse_expression(false);
	if (n_while->condition == nullptr) {
		push_error(R"(Expected conditional expression after "while".)");
	}

	consume(GDScriptTokenizer::Token::COLON, R"(Expected ":" after "while" condition.)");

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Allow break/continue.
	can_break = true;
	can_continue = true;

	SuiteNode *suite = alloc_node<SuiteNode>();
	suite->is_in_loop = true;
	n_while->loop = parse_suite(R"("while" block)", suite);
	complete_extents(n_while);

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return n_while;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_precedence(Precedence p_precedence, bool p_can_assign, bool p_stop_on_assign, bool p_stop_on_question_mark) {
	// Bail out before the recursive descent overflows the native stack on pathologically
	// nested expressions (e.g. thousands of nested parentheses), turning a crash into a
	// recoverable parse error.
	RecursionDepthGuard depth_guard(expression_nesting_depth);
	if (unlikely(expression_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Expression nesting is too deep.");
		return nullptr;
	}

	// Switch multiline mode on for grouping tokens.
	// Do this early to avoid the tokenizer generating whitespace tokens.
	switch (current.type) {
		case GDScriptTokenizer::Token::PARENTHESIS_OPEN:
		case GDScriptTokenizer::Token::BRACE_OPEN:
		case GDScriptTokenizer::Token::BRACKET_OPEN:
			push_multiline(true);
			break;
		default:
			break; // Nothing to do.
	}

	// Completion can appear whenever an expression is expected.
	make_completion_context(COMPLETION_IDENTIFIER, nullptr, -1, false);

	GDScriptTokenizer::Token token = current;
	GDScriptTokenizer::Token::Type token_type = token.type;
	if (token.is_identifier()) {
		// Allow keywords that can be treated as identifiers.
		token_type = GDScriptTokenizer::Token::IDENTIFIER;
	}
	ParseFunction prefix_rule = get_rule(token_type)->prefix;

	if (prefix_rule == nullptr) {
		// Expected expression. Let the caller give the proper error message.
		return nullptr;
	}

	advance(); // Only consume the token if there's a valid rule.

	// After a token was consumed, update the completion context regardless of a previously set context.

	ExpressionNode *previous_operand = (this->*prefix_rule)(nullptr, p_can_assign);

#ifdef TOOLS_ENABLED
	// HACK: We can't create a context in parse_identifier since it is used in places were we don't want completion.
	if (previous_operand != nullptr && previous_operand->type == GDScriptParser::Node::IDENTIFIER && prefix_rule == static_cast<ParseFunction>(&GDScriptParser::parse_identifier)) {
		make_completion_context(COMPLETION_IDENTIFIER, previous_operand);
	}
#endif

	while (p_precedence <= get_rule(current.type)->precedence) {
		if (previous_operand == nullptr || (p_stop_on_assign && current.type == GDScriptTokenizer::Token::EQUAL) || (p_stop_on_question_mark && current.type == GDScriptTokenizer::Token::QUESTION_MARK) || lambda_ended) {
			return previous_operand;
		}
		// Also switch multiline mode on here for infix operators.
		switch (current.type) {
			// case GDScriptTokenizer::Token::BRACE_OPEN: // Not an infix operator.
			case GDScriptTokenizer::Token::PARENTHESIS_OPEN:
			case GDScriptTokenizer::Token::BRACKET_OPEN:
				push_multiline(true);
				break;
			default:
				break; // Nothing to do.
		}
		token = advance();
		ParseFunction infix_rule = get_rule(token.type)->infix;
		previous_operand = (this->*infix_rule)(previous_operand, p_can_assign);
	}

	return previous_operand;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_expression(bool p_can_assign, bool p_stop_on_assign, bool p_stop_on_question_mark) {
	return parse_precedence(PREC_ASSIGNMENT, p_can_assign, p_stop_on_assign, p_stop_on_question_mark);
}

GDScriptParser::IdentifierNode *GDScriptParser::parse_identifier() {
	IdentifierNode *identifier = static_cast<IdentifierNode *>(parse_identifier(nullptr, false));
#ifdef DEBUG_ENABLED
	// Check for spoofing here (if available in TextServer) since this isn't called inside expressions. This is only relevant for declarations.
	if (identifier && TS->has_feature(TextServer::FEATURE_UNICODE_SECURITY) && TS->spoof_check(identifier->name)) {
		push_warning(identifier, GDScriptWarning::CONFUSABLE_IDENTIFIER, identifier->name.operator String());
	}
#endif
	return identifier;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_identifier(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (!previous.is_identifier()) {
		ERR_FAIL_V_MSG(nullptr, "Parser bug: parsing identifier node without identifier token.");
	}
	IdentifierNode *identifier = alloc_node<IdentifierNode>();
	complete_extents(identifier);
	identifier->name = previous.get_identifier();
	if (identifier->name.operator String().is_empty()) {
		print_line("Empty identifier found.");
	}
	identifier->suite = current_suite;

	if (current_suite != nullptr && current_suite->has_local(identifier->name)) {
		const SuiteNode::Local &declaration = current_suite->get_local(identifier->name);

		identifier->source_function = declaration.source_function;
		switch (declaration.type) {
			case SuiteNode::Local::CONSTANT:
				identifier->source = IdentifierNode::LOCAL_CONSTANT;
				identifier->constant_source = declaration.constant;
				declaration.constant->usages++;
				break;
			case SuiteNode::Local::VARIABLE:
				identifier->source = IdentifierNode::LOCAL_VARIABLE;
				identifier->variable_source = declaration.variable;
				declaration.variable->usages++;
				break;
			case SuiteNode::Local::PARAMETER:
				identifier->source = IdentifierNode::FUNCTION_PARAMETER;
				identifier->parameter_source = declaration.parameter;
				declaration.parameter->usages++;
				break;
			case SuiteNode::Local::FOR_VARIABLE:
				identifier->source = IdentifierNode::LOCAL_ITERATOR;
				identifier->bind_source = declaration.bind;
				declaration.bind->usages++;
				break;
			case SuiteNode::Local::PATTERN_BIND:
				identifier->source = IdentifierNode::LOCAL_BIND;
				identifier->bind_source = declaration.bind;
				declaration.bind->usages++;
				break;
			case SuiteNode::Local::UNDEFINED:
				ERR_FAIL_V_MSG(nullptr, "Undefined local found.");
		}
	}

	return identifier;
}

GDScriptParser::LiteralNode *GDScriptParser::parse_literal() {
	return static_cast<LiteralNode *>(parse_literal(nullptr, false));
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_literal(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (previous.type != GDScriptTokenizer::Token::LITERAL) {
		push_error("Parser bug: parsing literal node without literal token.");
		ERR_FAIL_V_MSG(nullptr, "Parser bug: parsing literal node without literal token.");
	}

	LiteralNode *literal = alloc_node<LiteralNode>();
	literal->value = previous.literal;
	reset_extents(literal, p_previous_operand);
	update_extents(literal);
	make_completion_context(COMPLETION_NONE, literal, -1);
	complete_extents(literal);
	return literal;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_self(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (current_function && current_function->is_static) {
		push_error(R"(Cannot use "self" inside a static function.)");
	}
	SelfNode *self = alloc_node<SelfNode>();
	complete_extents(self);
	self->current_class = current_class;
	return self;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_builtin_constant(ExpressionNode *p_previous_operand, bool p_can_assign) {
	GDScriptTokenizer::Token::Type op_type = previous.type;
	LiteralNode *constant = alloc_node<LiteralNode>();
	complete_extents(constant);

	switch (op_type) {
		case GDScriptTokenizer::Token::CONST_PI:
			constant->value = Math::PI;
			break;
		case GDScriptTokenizer::Token::CONST_TAU:
			constant->value = Math::TAU;
			break;
		case GDScriptTokenizer::Token::CONST_INF:
			constant->value = Math::INF;
			break;
		case GDScriptTokenizer::Token::CONST_NAN:
			constant->value = Math::NaN;
			break;
		default:
			return nullptr; // Unreachable.
	}

	return constant;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_unary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	GDScriptTokenizer::Token::Type op_type = previous.type;
	UnaryOpNode *operation = alloc_node<UnaryOpNode>();

	switch (op_type) {
		case GDScriptTokenizer::Token::MINUS:
			operation->operation = UnaryOpNode::OP_NEGATIVE;
			operation->variant_op = Variant::OP_NEGATE;
			operation->operand = parse_precedence(PREC_SIGN, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "-" operator.)");
			}
			break;
		case GDScriptTokenizer::Token::PLUS:
			operation->operation = UnaryOpNode::OP_POSITIVE;
			operation->variant_op = Variant::OP_POSITIVE;
			operation->operand = parse_precedence(PREC_SIGN, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "+" operator.)");
			}
			break;
		case GDScriptTokenizer::Token::TILDE:
			operation->operation = UnaryOpNode::OP_COMPLEMENT;
			operation->variant_op = Variant::OP_BIT_NEGATE;
			operation->operand = parse_precedence(PREC_BIT_NOT, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "~" operator.)");
			}
			break;
		case GDScriptTokenizer::Token::NOT:
		case GDScriptTokenizer::Token::BANG:
			operation->operation = UnaryOpNode::OP_LOGIC_NOT;
			operation->variant_op = Variant::OP_NOT;
			operation->operand = parse_precedence(PREC_LOGIC_NOT, false);
			if (operation->operand == nullptr) {
				push_error(vformat(R"(Expected expression after "%s" operator.)", op_type == GDScriptTokenizer::Token::NOT ? "not" : "!"));
			}
			break;
		default:
			complete_extents(operation);
			return nullptr; // Unreachable.
	}
	complete_extents(operation);

	return operation;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_binary_not_in_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// check that NOT is followed by IN by consuming it before calling parse_binary_operator which will only receive a plain IN
	UnaryOpNode *operation = alloc_node<UnaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);
	consume(GDScriptTokenizer::Token::TK_IN, R"(Expected "in" after "not" in content-test operator.)");
	ExpressionNode *in_operation = parse_binary_operator(p_previous_operand, p_can_assign);
	operation->operation = UnaryOpNode::OP_LOGIC_NOT;
	operation->variant_op = Variant::OP_NOT;
	operation->operand = in_operation;
	complete_extents(operation);
	return operation;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_binary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	GDScriptTokenizer::Token op = previous;
	BinaryOpNode *operation = alloc_node<BinaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);

	Precedence precedence = (Precedence)(get_rule(op.type)->precedence + 1);
	operation->left_operand = p_previous_operand;
	operation->right_operand = parse_precedence(precedence, false);
	complete_extents(operation);

	if (operation->right_operand == nullptr) {
		push_error(vformat(R"(Expected expression after "%s" operator.)", op.get_name()));
	}

	// TODO: Also for unary, ternary, and assignment.
	switch (op.type) {
		case GDScriptTokenizer::Token::PLUS:
			operation->operation = BinaryOpNode::OP_ADDITION;
			operation->variant_op = Variant::OP_ADD;
			break;
		case GDScriptTokenizer::Token::MINUS:
			operation->operation = BinaryOpNode::OP_SUBTRACTION;
			operation->variant_op = Variant::OP_SUBTRACT;
			break;
		case GDScriptTokenizer::Token::STAR:
			operation->operation = BinaryOpNode::OP_MULTIPLICATION;
			operation->variant_op = Variant::OP_MULTIPLY;
			break;
		case GDScriptTokenizer::Token::SLASH:
			operation->operation = BinaryOpNode::OP_DIVISION;
			operation->variant_op = Variant::OP_DIVIDE;
			break;
		case GDScriptTokenizer::Token::PERCENT:
			operation->operation = BinaryOpNode::OP_MODULO;
			operation->variant_op = Variant::OP_MODULE;
			break;
		case GDScriptTokenizer::Token::STAR_STAR:
			operation->operation = BinaryOpNode::OP_POWER;
			operation->variant_op = Variant::OP_POWER;
			break;
		case GDScriptTokenizer::Token::LESS_LESS:
			operation->operation = BinaryOpNode::OP_BIT_LEFT_SHIFT;
			operation->variant_op = Variant::OP_SHIFT_LEFT;
			break;
		case GDScriptTokenizer::Token::GREATER_GREATER:
			operation->operation = BinaryOpNode::OP_BIT_RIGHT_SHIFT;
			operation->variant_op = Variant::OP_SHIFT_RIGHT;
			break;
		case GDScriptTokenizer::Token::AMPERSAND:
			operation->operation = BinaryOpNode::OP_BIT_AND;
			operation->variant_op = Variant::OP_BIT_AND;
			break;
		case GDScriptTokenizer::Token::PIPE:
			operation->operation = BinaryOpNode::OP_BIT_OR;
			operation->variant_op = Variant::OP_BIT_OR;
			break;
		case GDScriptTokenizer::Token::CARET:
			operation->operation = BinaryOpNode::OP_BIT_XOR;
			operation->variant_op = Variant::OP_BIT_XOR;
			break;
		case GDScriptTokenizer::Token::AND:
		case GDScriptTokenizer::Token::AMPERSAND_AMPERSAND:
			operation->operation = BinaryOpNode::OP_LOGIC_AND;
			operation->variant_op = Variant::OP_AND;
			break;
		case GDScriptTokenizer::Token::OR:
		case GDScriptTokenizer::Token::PIPE_PIPE:
			operation->operation = BinaryOpNode::OP_LOGIC_OR;
			operation->variant_op = Variant::OP_OR;
			break;
		case GDScriptTokenizer::Token::TK_IN:
			operation->operation = BinaryOpNode::OP_CONTENT_TEST;
			operation->variant_op = Variant::OP_IN;
			break;
		case GDScriptTokenizer::Token::EQUAL_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_EQUAL;
			operation->variant_op = Variant::OP_EQUAL;
			break;
		case GDScriptTokenizer::Token::BANG_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_NOT_EQUAL;
			operation->variant_op = Variant::OP_NOT_EQUAL;
			break;
		case GDScriptTokenizer::Token::LESS:
			operation->operation = BinaryOpNode::OP_COMP_LESS;
			operation->variant_op = Variant::OP_LESS;
			break;
		case GDScriptTokenizer::Token::LESS_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_LESS_EQUAL;
			operation->variant_op = Variant::OP_LESS_EQUAL;
			break;
		case GDScriptTokenizer::Token::GREATER:
			operation->operation = BinaryOpNode::OP_COMP_GREATER;
			operation->variant_op = Variant::OP_GREATER;
			break;
		case GDScriptTokenizer::Token::GREATER_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_GREATER_EQUAL;
			operation->variant_op = Variant::OP_GREATER_EQUAL;
			break;
		default:
			return nullptr; // Unreachable.
	}

	return operation;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_ternary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// Only one ternary operation exists, so no abstraction here.
	TernaryOpNode *operation = alloc_node<TernaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);

	operation->true_expr = p_previous_operand;
	operation->condition = parse_precedence(PREC_TERNARY, false);

	if (operation->condition == nullptr) {
		push_error(R"(Expected expression as ternary condition after "if".)");
	}

	consume(GDScriptTokenizer::Token::ELSE, R"(Expected "else" after ternary operator condition.)");

	operation->false_expr = parse_precedence(PREC_TERNARY, false);

	if (operation->false_expr == nullptr) {
		push_error(R"(Expected expression after "else".)");
	}

	complete_extents(operation);
	return operation;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_assignment(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (!p_can_assign) {
		push_error("Assignment is not allowed inside an expression.");
		return parse_expression(false); // Return the following expression.
	}
	if (p_previous_operand == nullptr) {
		return parse_expression(false); // Return the following expression.
	}

	switch (p_previous_operand->type) {
		case Node::IDENTIFIER: {
#ifdef DEBUG_ENABLED
			// Get source to store assignment count.
			// Also remove one usage since assignment isn't usage.
			IdentifierNode *id = static_cast<IdentifierNode *>(p_previous_operand);
			switch (id->source) {
				case IdentifierNode::LOCAL_VARIABLE:
					id->variable_source->usages--;
					break;
				case IdentifierNode::LOCAL_CONSTANT:
					id->constant_source->usages--;
					break;
				case IdentifierNode::FUNCTION_PARAMETER:
					id->parameter_source->usages--;
					break;
				case IdentifierNode::LOCAL_ITERATOR:
				case IdentifierNode::LOCAL_BIND:
					id->bind_source->usages--;
					break;
				default:
					break;
			}
#endif
		} break;
		case Node::SUBSCRIPT:
			// Okay.
			break;
		default:
			push_error(R"(Only identifier, attribute access, and subscription access can be used as assignment target.)");
			return parse_expression(false); // Return the following expression.
	}

	AssignmentNode *assignment = alloc_node<AssignmentNode>();
	reset_extents(assignment, p_previous_operand);
	update_extents(assignment);

	make_completion_context(COMPLETION_ASSIGN, assignment);
	switch (previous.type) {
		case GDScriptTokenizer::Token::EQUAL:
			assignment->operation = AssignmentNode::OP_NONE;
			assignment->variant_op = Variant::OP_MAX;
			break;
		case GDScriptTokenizer::Token::PLUS_EQUAL:
			assignment->operation = AssignmentNode::OP_ADDITION;
			assignment->variant_op = Variant::OP_ADD;
			break;
		case GDScriptTokenizer::Token::MINUS_EQUAL:
			assignment->operation = AssignmentNode::OP_SUBTRACTION;
			assignment->variant_op = Variant::OP_SUBTRACT;
			break;
		case GDScriptTokenizer::Token::STAR_EQUAL:
			assignment->operation = AssignmentNode::OP_MULTIPLICATION;
			assignment->variant_op = Variant::OP_MULTIPLY;
			break;
		case GDScriptTokenizer::Token::STAR_STAR_EQUAL:
			assignment->operation = AssignmentNode::OP_POWER;
			assignment->variant_op = Variant::OP_POWER;
			break;
		case GDScriptTokenizer::Token::SLASH_EQUAL:
			assignment->operation = AssignmentNode::OP_DIVISION;
			assignment->variant_op = Variant::OP_DIVIDE;
			break;
		case GDScriptTokenizer::Token::PERCENT_EQUAL:
			assignment->operation = AssignmentNode::OP_MODULO;
			assignment->variant_op = Variant::OP_MODULE;
			break;
		case GDScriptTokenizer::Token::LESS_LESS_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_SHIFT_LEFT;
			assignment->variant_op = Variant::OP_SHIFT_LEFT;
			break;
		case GDScriptTokenizer::Token::GREATER_GREATER_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_SHIFT_RIGHT;
			assignment->variant_op = Variant::OP_SHIFT_RIGHT;
			break;
		case GDScriptTokenizer::Token::AMPERSAND_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_AND;
			assignment->variant_op = Variant::OP_BIT_AND;
			break;
		case GDScriptTokenizer::Token::PIPE_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_OR;
			assignment->variant_op = Variant::OP_BIT_OR;
			break;
		case GDScriptTokenizer::Token::CARET_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_XOR;
			assignment->variant_op = Variant::OP_BIT_XOR;
			break;
		default:
			break; // Unreachable.
	}
	assignment->assignee = p_previous_operand;
	assignment->assigned_value = parse_expression(false);
#ifdef TOOLS_ENABLED
	if (assignment->assigned_value != nullptr && assignment->assigned_value->type == GDScriptParser::Node::IDENTIFIER) {
		override_completion_context(assignment->assigned_value, COMPLETION_ASSIGN, assignment);
	}
#endif
	if (assignment->assigned_value == nullptr) {
		push_error(R"(Expected an expression after "=".)");
	}
	complete_extents(assignment);

	return assignment;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_await(ExpressionNode *p_previous_operand, bool p_can_assign) {
	AwaitNode *await = alloc_node<AwaitNode>();
	ExpressionNode *element = parse_precedence(PREC_AWAIT, false);
	if (element == nullptr) {
		push_error(R"(Expected signal or coroutine after "await".)");
	}
	await->to_await = element;
	complete_extents(await);

	if (current_function) { // Might be null in a getter or setter.
		current_function->is_coroutine = true;
	}

	return await;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_array(ExpressionNode *p_previous_operand, bool p_can_assign) {
	ArrayNode *array = alloc_node<ArrayNode>();

	if (!check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
		do {
			if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ExpressionNode *element = parse_expression(false);
			if (element == nullptr) {
				push_error(R"(Expected expression as array element.)");
			} else {
				array->elements.push_back(element);
			}
		} while (match(GDScriptTokenizer::Token::COMMA) && !is_at_end());
	}
	pop_multiline();
	consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after array elements.)");
	complete_extents(array);

	return array;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_dictionary(ExpressionNode *p_previous_operand, bool p_can_assign) {
	DictionaryNode *dictionary = alloc_node<DictionaryNode>();

	bool decided_style = false;
	if (!check(GDScriptTokenizer::Token::BRACE_CLOSE)) {
		do {
			if (check(GDScriptTokenizer::Token::BRACE_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			// Key.
			ExpressionNode *key = parse_expression(false, true); // Stop on "=" so we can check for Lua table style.

			if (key == nullptr) {
				push_error(R"(Expected expression as dictionary key.)");
			}

			if (!decided_style) {
				switch (current.type) {
					case GDScriptTokenizer::Token::COLON:
						dictionary->style = DictionaryNode::PYTHON_DICT;
						break;
					case GDScriptTokenizer::Token::EQUAL:
						dictionary->style = DictionaryNode::LUA_TABLE;
						break;
					default:
						push_error(R"(Expected ":" or "=" after dictionary key.)");
						break;
				}
				decided_style = true;
			}

			switch (dictionary->style) {
				case DictionaryNode::LUA_TABLE:
					if (key != nullptr && key->type != Node::IDENTIFIER && key->type != Node::LITERAL) {
						push_error(R"(Expected identifier or string as Lua-style dictionary key (e.g "{ key = value }").)");
					}
					if (key != nullptr && key->type == Node::LITERAL && static_cast<LiteralNode *>(key)->value.get_type() != Variant::STRING) {
						push_error(R"(Expected identifier or string as Lua-style dictionary key (e.g "{ key = value }").)");
					}
					if (!match(GDScriptTokenizer::Token::EQUAL)) {
						if (match(GDScriptTokenizer::Token::COLON)) {
							push_error(R"(Expected "=" after dictionary key. Mixing dictionary styles is not allowed.)");
							advance(); // Consume wrong separator anyway.
						} else {
							push_error(R"(Expected "=" after dictionary key.)");
						}
					}
					if (key != nullptr) {
						key->is_constant = true;
						if (key->type == Node::IDENTIFIER) {
							key->reduced_value = static_cast<IdentifierNode *>(key)->name;
						} else if (key->type == Node::LITERAL) {
							key->reduced_value = StringName(static_cast<LiteralNode *>(key)->value.operator String());
						}
					}
					break;
				case DictionaryNode::PYTHON_DICT:
					if (!match(GDScriptTokenizer::Token::COLON)) {
						if (match(GDScriptTokenizer::Token::EQUAL)) {
							push_error(R"(Expected ":" after dictionary key. Mixing dictionary styles is not allowed.)");
							advance(); // Consume wrong separator anyway.
						} else {
							push_error(R"(Expected ":" after dictionary key.)");
						}
					}
					break;
			}

			// Value.
			ExpressionNode *value = parse_expression(false);
			if (value == nullptr) {
				push_error(R"(Expected expression as dictionary value.)");
			}

			if (key != nullptr && value != nullptr) {
				dictionary->elements.push_back({ key, value });
			}

			// Do phrase level recovery by inserting an imaginary expression for missing keys or values.
			// This ensures the successfully parsed expression is part of the AST and can be analyzed.
			if (key != nullptr && value == nullptr) {
				LiteralNode *dummy = alloc_recovery_node<LiteralNode>();
				dummy->value = Variant();

				dictionary->elements.push_back({ key, dummy });
			} else if (key == nullptr && value != nullptr) {
				LiteralNode *dummy = alloc_recovery_node<LiteralNode>();
				dummy->value = Variant();

				dictionary->elements.push_back({ dummy, value });
			}

		} while (match(GDScriptTokenizer::Token::COMMA) && !is_at_end());
	}
	pop_multiline();
	consume(GDScriptTokenizer::Token::BRACE_CLOSE, R"(Expected closing "}" after dictionary elements.)");
	complete_extents(dictionary);

	return dictionary;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_grouping(ExpressionNode *p_previous_operand, bool p_can_assign) {
	ExpressionNode *grouped = parse_expression(false);
	pop_multiline();
	if (grouped == nullptr) {
		push_error(R"(Expected grouping expression.)");
	} else {
		consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after grouping expression.)*");
	}
	return grouped;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_attribute(ExpressionNode *p_previous_operand, bool p_can_assign) {
	SubscriptNode *attribute = alloc_node<SubscriptNode>();
	reset_extents(attribute, p_previous_operand);
	update_extents(attribute);

	if (for_completion) {
		bool is_builtin = false;
		if (p_previous_operand && p_previous_operand->type == Node::IDENTIFIER) {
			const IdentifierNode *id = static_cast<const IdentifierNode *>(p_previous_operand);
			Variant::Type builtin_type = get_builtin_type(id->name);
			if (builtin_type < Variant::VARIANT_MAX) {
				make_completion_context(COMPLETION_BUILT_IN_TYPE_CONSTANT_OR_STATIC_METHOD, builtin_type);
				is_builtin = true;
			}
		}
		if (!is_builtin) {
			make_completion_context(COMPLETION_ATTRIBUTE, attribute, -1);
		}
	}

	attribute->base = p_previous_operand;

	if (current.is_node_name()) {
		current.type = GDScriptTokenizer::Token::IDENTIFIER;
	}
	if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected identifier after "." for attribute access.)")) {
		complete_extents(attribute);
		return attribute;
	}

	attribute->is_attribute = true;
	attribute->attribute = parse_identifier();

	complete_extents(attribute);
	return attribute;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_subscript(ExpressionNode *p_previous_operand, bool p_can_assign) {
	SubscriptNode *subscript = alloc_node<SubscriptNode>();
	reset_extents(subscript, p_previous_operand);
	update_extents(subscript);

	make_completion_context(COMPLETION_SUBSCRIPT, subscript);

	subscript->base = p_previous_operand;
	// Stop before a trailing `?` so a use-site nullable type-argument marker (`Box[Node?]`) is not
	// swallowed by the Pratt loop as the always-invalid `?` infix; `parse_cast`/ternary indexing
	// (`arr[x as int]`, `arr[a if b else c]`) bind more tightly than `?` and still parse here.
	subscript->index = parse_expression(false, false, true);

#ifdef TOOLS_ENABLED
	if (subscript->index != nullptr && subscript->index->type == Node::LITERAL) {
		override_completion_context(subscript->index, COMPLETION_SUBSCRIPT, subscript);
	}
#endif

	if (subscript->index == nullptr) {
		push_error(R"(Expected expression after "[".)");
	} else if (check(GDScriptTokenizer::Token::QUESTION_MARK) || check(GDScriptTokenizer::Token::COMMA)) {
		// A use-site type-argument list such as `Pair[int, String]`, `id[Node?]`, or a Callable
		// signature argument. Ordinary indexing never uses commas or a trailing `?`, so this shape
		// is unambiguous here. Capture the full comma-separated list (with optional `?` markers) so
		// generic-class specialization and explicit generic-method application can read every
		// argument; `index` keeps aliasing the first element. Whether such a list is valid for the
		// subscripted base is decided during analysis, mirroring how `Box[int]` is interpreted.
		subscript->type_arguments.push_back(subscript->index);
		subscript->type_argument_is_nullable.push_back(match(GDScriptTokenizer::Token::QUESTION_MARK));
		while (match(GDScriptTokenizer::Token::COMMA) && !check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
			ExpressionNode *type_argument = parse_expression(false, false, true);
			if (type_argument == nullptr) {
				push_error(R"(Expected type argument after ",".)");
				break;
			}
			subscript->type_arguments.push_back(type_argument);
			subscript->type_argument_is_nullable.push_back(match(GDScriptTokenizer::Token::QUESTION_MARK));
		}
	}

	pop_multiline();
	consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected "]" after subscription index.)");
	complete_extents(subscript);

	return subscript;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_cast(ExpressionNode *p_previous_operand, bool p_can_assign) {
	CastNode *cast = alloc_node<CastNode>();
	reset_extents(cast, p_previous_operand);
	update_extents(cast);

	cast->operand = p_previous_operand;
	cast->cast_type = parse_type();
	complete_extents(cast);

	if (cast->cast_type == nullptr) {
		push_error(R"(Expected type specifier after "as".)");
		return p_previous_operand;
	}

	return cast;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_call(ExpressionNode *p_previous_operand, bool p_can_assign) {
	CallNode *call = alloc_node<CallNode>();
	reset_extents(call, p_previous_operand);

	if (previous.type == GDScriptTokenizer::Token::SUPER) {
		// Super call.
		call->is_super = true;
		if (!check(GDScriptTokenizer::Token::PERIOD)) {
			make_completion_context(COMPLETION_SUPER, call);
		}
		push_multiline(true);
		if (match(GDScriptTokenizer::Token::PARENTHESIS_OPEN)) {
			// Implicit call to the parent method of the same name.
			if (current_function == nullptr) {
				push_error(R"(Cannot use implicit "super" call outside of a function.)");
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
			if (current_function->identifier) {
				call->function_name = current_function->identifier->name;
			} else {
				call->function_name = SNAME("<anonymous>");
			}
		} else {
			consume(GDScriptTokenizer::Token::PERIOD, R"(Expected "." or "(" after "super".)");
			make_completion_context(COMPLETION_SUPER_METHOD, call);
			if (!consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected function name after ".".)")) {
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
			IdentifierNode *identifier = parse_identifier();
			call->callee = identifier;
			call->function_name = identifier->name;
			if (!consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after function name.)")) {
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
		}
	} else {
		call->callee = p_previous_operand;

		if (call->callee == nullptr) {
			push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*");
		} else if (call->callee->type == Node::IDENTIFIER) {
			call->function_name = static_cast<IdentifierNode *>(call->callee)->name;
			make_completion_context(COMPLETION_METHOD, call->callee);
		} else if (call->callee->type == Node::SUBSCRIPT) {
			SubscriptNode *attribute = static_cast<SubscriptNode *>(call->callee);
			if (attribute->is_attribute) {
				if (attribute->attribute) {
					call->function_name = attribute->attribute->name;
				}
				make_completion_context(COMPLETION_ATTRIBUTE_METHOD, call->callee);
			} else {
				// `expr[...](...)`: either a generic method application (`name[TypeArgs](...)` or
				// `receiver.method[TypeArgs](...)`, where the brackets are a use-site type-argument
				// list) or an invalid call on an index. The parser cannot tell them apart, so it
				// builds the call and lets call reduction decide and diagnose (mirroring how
				// `Array[int]` is read during analysis). Record the method name for the analyzer.
				if (attribute->base != nullptr && attribute->base->type == Node::IDENTIFIER) {
					call->function_name = static_cast<IdentifierNode *>(attribute->base)->name;
					make_completion_context(COMPLETION_METHOD, attribute->base);
				} else if (attribute->base != nullptr && attribute->base->type == Node::SUBSCRIPT) {
					// `receiver.method[TypeArgs](...)`: the type-argument list is applied to a method
					// accessed as an attribute on a receiver.
					SubscriptNode *receiver_access = static_cast<SubscriptNode *>(attribute->base);
					if (receiver_access->is_attribute && receiver_access->attribute != nullptr) {
						call->function_name = receiver_access->attribute->name;
						make_completion_context(COMPLETION_ATTRIBUTE_METHOD, receiver_access);
					}
				}
			}
		} else {
			push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*");
		}
	}

	// Arguments.
	CompletionType ct = COMPLETION_CALL_ARGUMENTS;
	if (call->function_name == SNAME("load")) {
		ct = COMPLETION_RESOURCE_PATH;
	}
	push_completion_call(call);
	int argument_index = 0;
	do {
		make_completion_context(ct, call, argument_index);
		set_last_completion_call_arg(argument_index);
		if (check(GDScriptTokenizer::Token::PARENTHESIS_CLOSE)) {
			// Allow for trailing comma.
			break;
		}
		// Stop on a trailing "=" so a leading identifier followed by "=" can be read as a
		// named argument (`name = value`). GDScript assignment is a statement, never an
		// expression, so `IDENTIFIER` + `EQUAL` here is unambiguously a named argument;
		// `f(a == b)` uses `EQUAL_EQUAL` and `f(a)` has no following "=".
		ExpressionNode *argument = parse_expression(false, true);
		StringName argument_name;
		if (argument != nullptr && check(GDScriptTokenizer::Token::EQUAL)) {
			if (argument->type == Node::IDENTIFIER) {
				argument_name = static_cast<IdentifierNode *>(argument)->name;
				advance(); // Consume "=".
				make_completion_context(ct, call, argument_index);
				argument = parse_expression(false);
				if (argument == nullptr) {
					push_error(vformat(R"(Expected expression after "%s =" named argument.)", argument_name));
				}
			} else {
				// A non-identifier target before "=" is an attempted assignment, which is not
				// a valid expression argument. Consume the rest so the stream recovers cleanly.
				push_error(R"(Assignment is not allowed inside an expression.)");
				advance(); // Consume "=".
				parse_expression(false);
			}
		}
#ifdef TOOLS_ENABLED
		// Record the surface argument name for each slot for editor code completion, indexed
		// by slot so it stays aligned with the completion's current argument. This is captured
		// even when the value is still missing (e.g. the cursor sits right after `name =`),
		// which `argument_names` below cannot represent, and it survives the analyzer's later
		// canonicalization that clears `argument_names`.
		if (call->parsed_argument_names.size() <= argument_index) {
			call->parsed_argument_names.resize(argument_index + 1);
		}
		call->parsed_argument_names.write[argument_index] = argument_name;
#endif // TOOLS_ENABLED

		if (argument == nullptr) {
			if (argument_name == StringName()) {
				push_error(R"(Expected expression as the function argument.)");
			}
		} else {
			call->arguments.push_back(argument);
			call->argument_names.push_back(argument_name);

			if (argument->type == Node::LITERAL) {
				override_completion_context(argument, ct, call, argument_index);
			}
		}

		ct = COMPLETION_CALL_ARGUMENTS;
		argument_index++;
	} while (match(GDScriptTokenizer::Token::COMMA));
	pop_completion_call();

	pop_multiline();
	consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after call arguments.)*");
	complete_extents(call);

	return call;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_get_node(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// We want code completion after a DOLLAR even if the current code is invalid.
	make_completion_context(COMPLETION_GET_NODE, nullptr, -1);

	if (!current.is_node_name() && !check(GDScriptTokenizer::Token::LITERAL) && !check(GDScriptTokenizer::Token::SLASH) && !check(GDScriptTokenizer::Token::PERCENT)) {
		push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous.get_name()));
		return nullptr;
	}

	if (check(GDScriptTokenizer::Token::LITERAL)) {
		if (current.literal.get_type() != Variant::STRING) {
			push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous.get_name()));
			return nullptr;
		}
	}

	GetNodeNode *get_node = alloc_node<GetNodeNode>();

	// Store the last item in the path so the parser knows what to expect.
	// Allow allows more specific error messages.
	enum PathState {
		PATH_STATE_START,
		PATH_STATE_SLASH,
		PATH_STATE_PERCENT,
		PATH_STATE_NODE_NAME,
	} path_state = PATH_STATE_START;

	if (previous.type == GDScriptTokenizer::Token::DOLLAR) {
		// Detect initial slash, which will be handled in the loop if it matches.
		match(GDScriptTokenizer::Token::SLASH);
	} else {
		get_node->use_dollar = false;
	}

	int context_argument = 0;

	do {
		if (previous.type == GDScriptTokenizer::Token::PERCENT) {
			if (path_state != PATH_STATE_START && path_state != PATH_STATE_SLASH) {
				push_error(R"("%" is only valid in the beginning of a node name (either after "$" or after "/"))");
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += "%";

			path_state = PATH_STATE_PERCENT;
		} else if (previous.type == GDScriptTokenizer::Token::SLASH) {
			if (path_state != PATH_STATE_START && path_state != PATH_STATE_NODE_NAME) {
				push_error(R"("/" is only valid at the beginning of the path or after a node name.)");
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += "/";

			path_state = PATH_STATE_SLASH;
		}

		make_completion_context(COMPLETION_GET_NODE, get_node, context_argument++);

		if (match(GDScriptTokenizer::Token::LITERAL)) {
			if (previous.literal.get_type() != Variant::STRING) {
				String previous_token;
				switch (path_state) {
					case PATH_STATE_START:
						previous_token = "$";
						break;
					case PATH_STATE_PERCENT:
						previous_token = "%";
						break;
					case PATH_STATE_SLASH:
						previous_token = "/";
						break;
					default:
						break;
				}
				push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous_token));
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += previous.literal.operator String();

			path_state = PATH_STATE_NODE_NAME;
		} else if (current.is_node_name()) {
			advance();

			String identifier = previous.get_identifier();
#ifdef DEBUG_ENABLED
			// Check spoofing.
			if (TS->has_feature(TextServer::FEATURE_UNICODE_SECURITY) && TS->spoof_check(identifier)) {
				push_warning(get_node, GDScriptWarning::CONFUSABLE_IDENTIFIER, identifier);
			}
#endif
			get_node->full_path += identifier;

			path_state = PATH_STATE_NODE_NAME;
		} else if (!check(GDScriptTokenizer::Token::SLASH) && !check(GDScriptTokenizer::Token::PERCENT)) {
			push_error(vformat(R"(Unexpected "%s" in node path.)", current.get_name()));
			complete_extents(get_node);
			return nullptr;
		}
	} while (match(GDScriptTokenizer::Token::SLASH) || match(GDScriptTokenizer::Token::PERCENT));

	complete_extents(get_node);
	return get_node;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_preload(ExpressionNode *p_previous_operand, bool p_can_assign) {
	PreloadNode *preload = alloc_node<PreloadNode>();
	preload->resolved_path = "<missing path>";

	push_multiline(true);
	consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "preload".)");

	make_completion_context(COMPLETION_RESOURCE_PATH, preload);
	push_completion_call(preload);

	preload->path = parse_expression(false);

	if (preload->path == nullptr) {
		push_error(R"(Expected resource path after "(".)");
	} else if (preload->path->type == Node::LITERAL) {
		override_completion_context(preload->path, COMPLETION_RESOURCE_PATH, preload);
	}

	pop_completion_call();

	// Allow trailing comma.
	match(GDScriptTokenizer::Token::COMMA);

	pop_multiline();
	consume(GDScriptTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after preload path.)*");
	complete_extents(preload);

	return preload;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_lambda(ExpressionNode *p_previous_operand, bool p_can_assign) {
	LambdaNode *lambda = alloc_node<LambdaNode>();
	lambda->parent_function = current_function;
	lambda->parent_lambda = current_lambda;

	FunctionNode *function = alloc_node<FunctionNode>();
	function->source_lambda = lambda;

	function->is_static = current_function != nullptr ? current_function->is_static : false;

	if (match(GDScriptTokenizer::Token::IDENTIFIER)) {
		function->identifier = parse_identifier();
	}

	bool multiline_context = multiline_stack.back()->get();

	push_completion_call(nullptr);

	// Reset the multiline stack since we don't want the multiline mode one in the lambda body.
	push_multiline(false);
	if (multiline_context) {
		tokenizer->push_expression_indented_block();
	}

	push_multiline(true); // For the parameters.
	if (function->identifier) {
		consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after lambda name.)");
	} else {
		consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after "func".)");
	}

	FunctionNode *previous_function = current_function;
	current_function = function;

	LambdaNode *previous_lambda = current_lambda;
	current_lambda = lambda;

	SuiteNode *body = alloc_node<SuiteNode>();
	body->parent_function = current_function;
	body->parent_block = current_suite;

	SuiteNode *previous_suite = current_suite;
	current_suite = body;

	parse_function_signature(function, body, "lambda", -1);

	current_suite = previous_suite;

	bool previous_in_lambda = in_lambda;
	in_lambda = true;

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Disallow break/continue.
	can_break = false;
	can_continue = false;

	function->body = parse_suite("lambda declaration", body, true);
	complete_extents(function);
	complete_extents(lambda);

	pop_multiline();

	pop_completion_call();

	if (multiline_context) {
		// If we're in multiline mode, we want to skip the spurious DEDENT and NEWLINE tokens.
		while (check(GDScriptTokenizer::Token::DEDENT) || check(GDScriptTokenizer::Token::INDENT) || check(GDScriptTokenizer::Token::NEWLINE)) {
			current = tokenizer->scan(); // Not advance() since we don't want to change the previous token.
		}
		tokenizer->pop_expression_indented_block();
	}

	current_function = previous_function;
	current_lambda = previous_lambda;
	in_lambda = previous_in_lambda;
	lambda->function = function;

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return lambda;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_type_test(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// x is not int
	// ^        ^^^ ExpressionNode, TypeNode
	// ^^^^^^^^^^^^ TypeTestNode
	// ^^^^^^^^^^^^ UnaryOpNode
	UnaryOpNode *not_node = nullptr;
	if (match(GDScriptTokenizer::Token::NOT)) {
		not_node = alloc_node<UnaryOpNode>();
		not_node->operation = UnaryOpNode::OP_LOGIC_NOT;
		not_node->variant_op = Variant::OP_NOT;
		reset_extents(not_node, p_previous_operand);
		update_extents(not_node);
	}

	TypeTestNode *type_test = alloc_node<TypeTestNode>();
	reset_extents(type_test, p_previous_operand);
	update_extents(type_test);

	type_test->operand = p_previous_operand;
	type_test->test_type = parse_type();
	complete_extents(type_test);

	if (not_node != nullptr) {
		not_node->operand = type_test;
		complete_extents(not_node);
	}

	if (type_test->test_type == nullptr) {
		if (not_node == nullptr) {
			push_error(R"(Expected type specifier after "is".)");
		} else {
			push_error(R"(Expected type specifier after "is not".)");
		}
	}

	if (not_node != nullptr) {
		return not_node;
	}

	return type_test;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_yield(ExpressionNode *p_previous_operand, bool p_can_assign) {
	push_error(R"("yield" was removed in Godot 4. Use "await" instead.)");
	return nullptr;
}

GDScriptParser::ExpressionNode *GDScriptParser::parse_invalid_token(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// Just for better error messages.
	GDScriptTokenizer::Token::Type invalid = previous.type;

	switch (invalid) {
		case GDScriptTokenizer::Token::QUESTION_MARK:
			push_error(R"(Unexpected "?" in source. If you want a ternary operator, use "truthy_value if true_condition else falsy_value".)");
			break;
		default:
			return nullptr; // Unreachable.
	}

	// Return the previous expression.
	return p_previous_operand;
}

GDScriptParser::TypeNode *GDScriptParser::parse_type(bool p_allow_void) {
	// Nested type annotations (e.g. `Array[Array[...]]`, Callable/Coroutine signatures)
	// recurse through parse_type(); bound that depth so a pathologically nested type
	// reports an error instead of overflowing the native stack.
	RecursionDepthGuard depth_guard(type_nesting_depth);
	if (unlikely(type_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Type nesting is too deep.");
		return nullptr;
	}

	TypeNode *type = alloc_node<TypeNode>();
	make_completion_context(p_allow_void ? COMPLETION_TYPE_NAME_OR_VOID : COMPLETION_TYPE_NAME, type);
	if (!match(GDScriptTokenizer::Token::IDENTIFIER)) {
		if (match(GDScriptTokenizer::Token::TK_VOID)) {
			if (p_allow_void) {
				complete_extents(type);
				TypeNode *void_type = type;
				return void_type;
			} else {
				push_error(R"("void" is only allowed for a function return type.)");
			}
		}
		// Leave error message to the caller who knows the context.
		complete_extents(type);
		return nullptr;
	}

	IdentifierNode *type_element = parse_identifier();

	type->type_chain.push_back(type_element);

	// AsyncCallable mirrors Callable typing but carries an async marker for structured concurrency.
	if (type->type_chain.size() == 1 && type_element->name == SNAME("AsyncCallable")) {
		type->signature_is_async = true;
	}

	if (match(GDScriptTokenizer::Token::BRACKET_OPEN)) {
		const bool is_callable_type = type->type_chain.size() == 1 && (type_element->name == SNAME("Callable") || type_element->name == SNAME("AsyncCallable"));
		const bool is_signal_type = type->type_chain.size() == 1 && type_element->name == SNAME("Signal");
		// Coroutine[T] is the typed handle to an in-flight async computation for structured concurrency.
		const bool is_coroutine_type = type->type_chain.size() == 1 && type_element->name == SNAME("Coroutine");
		if ((is_callable_type || is_signal_type) && match(GDScriptTokenizer::Token::BRACKET_OPEN)) {
			type->has_signature = true;
			if (!check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				bool first_pass = true;
				do {
					TypeNode *parameter_type = parse_type(false);
					if (parameter_type == nullptr) {
						push_error(vformat(R"(Expected parameter type for signature after "%s".)", first_pass ? "[" : ","));
						break;
					}
					type->signature_parameter_types.append(parameter_type);
					first_pass = false;
				} while (match(GDScriptTokenizer::Token::COMMA) && !check(GDScriptTokenizer::Token::BRACKET_CLOSE));
			}
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after signature parameter types.)");
			if (is_callable_type) {
				consume(GDScriptTokenizer::Token::COMMA, R"(Expected "," after Callable signature parameter types.)");
				type->signature_return_type = parse_type(true);
				if (type->signature_return_type == nullptr) {
					push_error("Expected return type for Callable signature.");
				}
			} else if (match(GDScriptTokenizer::Token::COMMA)) {
				push_error("Signal signatures cannot specify a return type.");
				parse_type(true);
			}
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after signature type.)");
			if (match(GDScriptTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
			return type;
		}

		// Coroutine[T] carries exactly one phantom result type, unlike the variadic typed collections.
		if (is_coroutine_type) {
			type->is_coroutine = true;
			if (check(GDScriptTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Coroutine[T] expects a single result type parameter.)");
			} else {
				TypeNode *result_type = parse_type(true); // Allow void so void-returning async work is nameable as Coroutine[void].
				if (result_type == nullptr) {
					push_error(R"(Expected result type for Coroutine after "[".)");
				} else {
					type->container_types.append(result_type);
				}
				if (check(GDScriptTokenizer::Token::COMMA)) {
					push_error(R"(Coroutine[T] expects a single result type parameter, but more were given.)");
					while (match(GDScriptTokenizer::Token::COMMA)) {
						parse_type(false); // Consume the extra parameters so parsing stays aligned.
					}
				}
			}
			consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after Coroutine result type.)");
			if (match(GDScriptTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
			return type;
		}

		// Typed collection (like Array[int], Dictionary[String, int]).
		bool first_pass = true;
		do {
			TypeNode *container_type = parse_type(false); // Don't allow void for element type.
			if (container_type == nullptr) {
				push_error(vformat(R"(Expected type for collection after "%s".)", first_pass ? "[" : ","));
				complete_extents(type);
				type = nullptr;
				break;
			} else {
				type->container_types.append(container_type);
			}
			first_pass = false;
		} while (match(GDScriptTokenizer::Token::COMMA));
		consume(GDScriptTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after collection type.)");
		if (type != nullptr) {
			if (match(GDScriptTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
		}
		return type;
	}

	int chain_index = 1;
	while (match(GDScriptTokenizer::Token::PERIOD)) {
		make_completion_context(COMPLETION_TYPE_ATTRIBUTE, type, chain_index++);
		if (consume(GDScriptTokenizer::Token::IDENTIFIER, R"(Expected inner type name after ".".)")) {
			type_element = parse_identifier();
			type->type_chain.push_back(type_element);
		}
	}

	if (match(GDScriptTokenizer::Token::QUESTION_MARK)) {
		type->is_nullable = true;
	}
	complete_extents(type);
	return type;
}

#ifdef TOOLS_ENABLED
enum DocLineState {
	DOC_LINE_NORMAL,
	DOC_LINE_IN_CODE,
	DOC_LINE_IN_CODEBLOCK,
	DOC_LINE_IN_KBD,
};

static String _process_doc_line(const String &p_line, const String &p_text, const String &p_space_prefix, DocLineState &r_state) {
	String line = p_line;
	if (r_state == DOC_LINE_NORMAL) {
		line = line.strip_edges(true, false);
	} else {
		line = line.trim_prefix(p_space_prefix);
	}

	String line_join;
	if (!p_text.is_empty()) {
		if (r_state == DOC_LINE_NORMAL) {
			if (p_text.ends_with("[/codeblock]")) {
				line_join = "\n";
			} else if (!p_text.ends_with("[br]")) {
				line_join = " ";
			}
		} else {
			line_join = "\n";
		}
	}

	String result;
	int from = 0;
	int buffer_start = 0;
	const int len = line.length();
	bool process = true;
	while (process) {
		switch (r_state) {
			case DOC_LINE_NORMAL: {
				int lb_pos = line.find_char('[', from);
				if (lb_pos < 0) {
					process = false;
					break;
				}
				int rb_pos = line.find_char(']', lb_pos + 1);
				if (rb_pos < 0) {
					process = false;
					break;
				}

				from = rb_pos + 1;

				String tag = line.substr(lb_pos + 1, rb_pos - lb_pos - 1);
				if (tag == "code" || tag.begins_with("code ")) {
					r_state = DOC_LINE_IN_CODE;
				} else if (tag == "codeblock" || tag.begins_with("codeblock ")) {
					if (lb_pos == 0) {
						line_join = "\n";
					} else {
						result += line.substr(buffer_start, lb_pos - buffer_start) + '\n';
					}
					result += "[" + tag + "]";
					if (from < len) {
						result += '\n';
					}

					r_state = DOC_LINE_IN_CODEBLOCK;
					buffer_start = from;
				} else if (tag == "kbd") {
					r_state = DOC_LINE_IN_KBD;
				}
			} break;
			case DOC_LINE_IN_CODE: {
				int pos = line.find("[/code]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 7; // `len("[/code]")`.

				r_state = DOC_LINE_NORMAL;
			} break;
			case DOC_LINE_IN_CODEBLOCK: {
				int pos = line.find("[/codeblock]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 12; // `len("[/codeblock]")`.

				if (pos == 0) {
					line_join = "\n";
				} else {
					result += line.substr(buffer_start, pos - buffer_start) + '\n';
				}
				result += "[/codeblock]";
				if (from < len) {
					result += '\n';
				}

				r_state = DOC_LINE_NORMAL;
				buffer_start = from;
			} break;
			case DOC_LINE_IN_KBD: {
				int pos = line.find("[/kbd]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 6; // `len("[/kbd]")`.

				r_state = DOC_LINE_NORMAL;
			} break;
		}
	}

	result += line.substr(buffer_start);
	if (r_state == DOC_LINE_NORMAL) {
		result = result.strip_edges(false, true);
	}

	return line_join + result;
}

bool GDScriptParser::has_comment(int p_line, bool p_must_be_doc) {
	bool has_comment = tokenizer->get_comments().has(p_line);
	// If there are no comments or if we don't care whether the comment
	// is a docstring, we have our result.
	if (!p_must_be_doc || !has_comment) {
		return has_comment;
	}

	return tokenizer->get_comments()[p_line].comment.begins_with("##");
}

GDScriptParser::MemberDocData GDScriptParser::parse_doc_comment(int p_line, bool p_single_line) {
	ERR_FAIL_COND_V(!has_comment(p_line, true), MemberDocData());

	const HashMap<int, GDScriptTokenizer::CommentData> &comments = tokenizer->get_comments();
	int line = p_line;

	if (!p_single_line) {
		while (comments.has(line - 1) && comments[line - 1].new_line && comments[line - 1].comment.begins_with("##")) {
			line--;
		}
	}

	max_script_doc_line = MIN(max_script_doc_line, line - 1);

	String space_prefix;
	{
		int i = 2;
		for (; i < comments[line].comment.length(); i++) {
			if (comments[line].comment[i] != ' ') {
				break;
			}
		}
		space_prefix = String(" ").repeat(i - 2);
	}

	DocLineState state = DOC_LINE_NORMAL;
	MemberDocData result;

	while (line <= p_line) {
		String doc_line = comments[line].comment.trim_prefix("##");
		line++;

		if (state == DOC_LINE_NORMAL) {
			String stripped_line = doc_line.strip_edges();
			if (stripped_line == "@deprecated" || stripped_line.begins_with("@deprecated:")) {
				result.is_deprecated = true;
				if (stripped_line.begins_with("@deprecated:")) {
					result.deprecated_message = stripped_line.trim_prefix("@deprecated:").strip_edges();
				}
				continue;
			} else if (stripped_line == "@experimental" || stripped_line.begins_with("@experimental:")) {
				result.is_experimental = true;
				if (stripped_line.begins_with("@experimental:")) {
					result.experimental_message = stripped_line.trim_prefix("@experimental:").strip_edges();
				}
				continue;
			}
		}

		result.description += _process_doc_line(doc_line, result.description, space_prefix, state);
	}

	return result;
}

GDScriptParser::ClassDocData GDScriptParser::parse_class_doc_comment(int p_line, bool p_single_line) {
	ERR_FAIL_COND_V(!has_comment(p_line, true), ClassDocData());

	const HashMap<int, GDScriptTokenizer::CommentData> &comments = tokenizer->get_comments();
	int line = p_line;

	if (!p_single_line) {
		while (comments.has(line - 1) && comments[line - 1].new_line && comments[line - 1].comment.begins_with("##")) {
			line--;
		}
	}

	max_script_doc_line = MIN(max_script_doc_line, line - 1);

	String space_prefix;
	{
		int i = 2;
		for (; i < comments[line].comment.length(); i++) {
			if (comments[line].comment[i] != ' ') {
				break;
			}
		}
		space_prefix = String(" ").repeat(i - 2);
	}

	DocLineState state = DOC_LINE_NORMAL;
	bool is_in_brief = true;
	ClassDocData result;

	while (line <= p_line) {
		String doc_line = comments[line].comment.trim_prefix("##");
		line++;

		if (state == DOC_LINE_NORMAL) {
			String stripped_line = doc_line.strip_edges();

			// A blank line separates the description from the brief.
			if (is_in_brief && !result.brief.is_empty() && stripped_line.is_empty()) {
				is_in_brief = false;
				continue;
			}

			if (stripped_line.begins_with("@tutorial")) {
				String title, link;

				int begin_scan = String("@tutorial").length();
				if (begin_scan >= stripped_line.length()) {
					continue; // Invalid syntax.
				}

				if (stripped_line[begin_scan] == ':') { // No title.
					// Syntax: ## @tutorial: https://godotengine.org/ // The title argument is optional.
					title = "";
					link = stripped_line.trim_prefix("@tutorial:").strip_edges();
				} else {
					/* Syntax:
					 *   @tutorial ( The Title Here )         :         https://the.url/
					 *             ^ open           ^ close   ^ colon   ^ url
					 */
					int open_bracket_pos = begin_scan, close_bracket_pos = 0;
					while (open_bracket_pos < stripped_line.length() && (stripped_line[open_bracket_pos] == ' ' || stripped_line[open_bracket_pos] == '\t')) {
						open_bracket_pos++;
					}
					if (open_bracket_pos == stripped_line.length() || stripped_line[open_bracket_pos++] != '(') {
						continue; // Invalid syntax.
					}
					close_bracket_pos = open_bracket_pos;
					while (close_bracket_pos < stripped_line.length() && stripped_line[close_bracket_pos] != ')') {
						close_bracket_pos++;
					}
					if (close_bracket_pos == stripped_line.length()) {
						continue; // Invalid syntax.
					}

					int colon_pos = close_bracket_pos + 1;
					while (colon_pos < stripped_line.length() && (stripped_line[colon_pos] == ' ' || stripped_line[colon_pos] == '\t')) {
						colon_pos++;
					}
					if (colon_pos == stripped_line.length() || stripped_line[colon_pos++] != ':') {
						continue; // Invalid syntax.
					}

					title = stripped_line.substr(open_bracket_pos, close_bracket_pos - open_bracket_pos).strip_edges();
					link = stripped_line.substr(colon_pos).strip_edges();
				}

				result.tutorials.append(Pair<String, String>(title, link));
				continue;
			} else if (stripped_line == "@deprecated" || stripped_line.begins_with("@deprecated:")) {
				result.is_deprecated = true;
				if (stripped_line.begins_with("@deprecated:")) {
					result.deprecated_message = stripped_line.trim_prefix("@deprecated:").strip_edges();
				}
				continue;
			} else if (stripped_line == "@experimental" || stripped_line.begins_with("@experimental:")) {
				result.is_experimental = true;
				if (stripped_line.begins_with("@experimental:")) {
					result.experimental_message = stripped_line.trim_prefix("@experimental:").strip_edges();
				}
				continue;
			}
		}

		if (is_in_brief) {
			result.brief += _process_doc_line(doc_line, result.brief, space_prefix, state);
		} else {
			result.description += _process_doc_line(doc_line, result.description, space_prefix, state);
		}
	}

	return result;
}
#endif // TOOLS_ENABLED

GDScriptParser::ParseRule *GDScriptParser::get_rule(GDScriptTokenizer::Token::Type p_token_type) {
	// Function table for expression parsing.
	// clang-format destroys the alignment here, so turn off for the table.
	/* clang-format off */
	static ParseRule rules[] = {
		// PREFIX                                           INFIX                                           PRECEDENCE (for infix)
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // EMPTY,
		// Basic
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ANNOTATION,
		{ &GDScriptParser::parse_identifier,             	nullptr,                                        PREC_NONE }, // IDENTIFIER,
		{ &GDScriptParser::parse_literal,                	nullptr,                                        PREC_NONE }, // LITERAL,
		// Comparison
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // LESS,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // LESS_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // GREATER,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // GREATER_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // EQUAL_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_COMPARISON }, // BANG_EQUAL,
		// Logical
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_LOGIC_AND }, // AND,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_LOGIC_OR }, // OR,
		{ &GDScriptParser::parse_unary_operator,         	&GDScriptParser::parse_binary_not_in_operator,	PREC_CONTENT_TEST }, // NOT,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,			PREC_LOGIC_AND }, // AMPERSAND_AMPERSAND,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,			PREC_LOGIC_OR }, // PIPE_PIPE,
		{ &GDScriptParser::parse_unary_operator,			nullptr,                                        PREC_NONE }, // BANG,
		// Bitwise
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_BIT_AND }, // AMPERSAND,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_BIT_OR }, // PIPE,
		{ &GDScriptParser::parse_unary_operator,         	nullptr,                                        PREC_NONE }, // TILDE,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_BIT_XOR }, // CARET,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_BIT_SHIFT }, // LESS_LESS,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_BIT_SHIFT }, // GREATER_GREATER,
		// Math
		{ &GDScriptParser::parse_unary_operator,         	&GDScriptParser::parse_binary_operator,      	PREC_ADDITION_SUBTRACTION }, // PLUS,
		{ &GDScriptParser::parse_unary_operator,         	&GDScriptParser::parse_binary_operator,      	PREC_ADDITION_SUBTRACTION }, // MINUS,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_FACTOR }, // STAR,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_POWER }, // STAR_STAR,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_FACTOR }, // SLASH,
		{ &GDScriptParser::parse_get_node,                  &GDScriptParser::parse_binary_operator,      	PREC_FACTOR }, // PERCENT,
		// Assignment
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // PLUS_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // MINUS_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // STAR_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // STAR_STAR_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // SLASH_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // PERCENT_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // LESS_LESS_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // GREATER_GREATER_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // AMPERSAND_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // PIPE_EQUAL,
		{ nullptr,                                          &GDScriptParser::parse_assignment,           	PREC_ASSIGNMENT }, // CARET_EQUAL,
		// Control flow
		{ nullptr,                                          &GDScriptParser::parse_ternary_operator,     	PREC_TERNARY }, // IF,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ELIF,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ELSE,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // FOR,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // WHILE,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // BREAK,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // CONTINUE,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // PASS,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // RETURN,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // MATCH,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // WHEN,
		// Keywords
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ABSTRACT,
		{ nullptr,                                          &GDScriptParser::parse_cast,                 	PREC_CAST }, // AS,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ASSERT,
		{ &GDScriptParser::parse_await,                  	nullptr,                                        PREC_NONE }, // AWAIT,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // BREAKPOINT,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // CLASS,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // CLASS_NAME,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ENUM_NAME,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // TK_CONST,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ENUM,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // EXTENDS,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // FINAL,
		{ &GDScriptParser::parse_lambda,                    nullptr,                                        PREC_NONE }, // FUNC,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // IMPORT,
		{ nullptr,                                          &GDScriptParser::parse_binary_operator,      	PREC_CONTENT_TEST }, // TK_IN,
		{ nullptr,                                          &GDScriptParser::parse_type_test,            	PREC_TYPE_TEST }, // IS,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // NAMESPACE,
		{ &GDScriptParser::parse_preload,					nullptr,                                        PREC_NONE }, // PRELOAD,
		{ &GDScriptParser::parse_self,                   	nullptr,                                        PREC_NONE }, // SELF,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // SIGNAL,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // STATIC,
		{ &GDScriptParser::parse_call,						nullptr,                                        PREC_NONE }, // SUPER,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // TRAIT,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // TRAIT_NAME,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // USES,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // VAR,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // TK_VOID,
		{ &GDScriptParser::parse_yield,                     nullptr,                                        PREC_NONE }, // YIELD,
		// Punctuation
		{ &GDScriptParser::parse_array,                  	&GDScriptParser::parse_subscript,            	PREC_SUBSCRIPT }, // BRACKET_OPEN,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // BRACKET_CLOSE,
		{ &GDScriptParser::parse_dictionary,             	nullptr,                                        PREC_NONE }, // BRACE_OPEN,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // BRACE_CLOSE,
		{ &GDScriptParser::parse_grouping,               	&GDScriptParser::parse_call,                 	PREC_CALL }, // PARENTHESIS_OPEN,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // PARENTHESIS_CLOSE,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // COMMA,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // SEMICOLON,
		{ nullptr,                                          &GDScriptParser::parse_attribute,            	PREC_ATTRIBUTE }, // PERIOD,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // PERIOD_PERIOD,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // PERIOD_PERIOD_PERIOD,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // COLON,
		{ &GDScriptParser::parse_get_node,               	nullptr,                                        PREC_NONE }, // DOLLAR,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // FORWARD_ARROW,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // UNDERSCORE,
		// Whitespace
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // NEWLINE,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // INDENT,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // DEDENT,
		// Constants
		{ &GDScriptParser::parse_builtin_constant,			nullptr,                                        PREC_NONE }, // CONST_PI,
		{ &GDScriptParser::parse_builtin_constant,			nullptr,                                        PREC_NONE }, // CONST_TAU,
		{ &GDScriptParser::parse_builtin_constant,			nullptr,                                        PREC_NONE }, // CONST_INF,
		{ &GDScriptParser::parse_builtin_constant,			nullptr,                                        PREC_NONE }, // CONST_NAN,
		// Error message improvement
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // VCS_CONFLICT_MARKER,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // BACKTICK,
		{ nullptr,                                          &GDScriptParser::parse_invalid_token,        	PREC_CAST }, // QUESTION_MARK,
		// Special
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // ERROR,
		{ nullptr,                                          nullptr,                                        PREC_NONE }, // TK_EOF,
	};
	/* clang-format on */
	// Avoid desync.
	static_assert(std_size(rules) == GDScriptTokenizer::Token::TK_MAX, "Amount of parse rules don't match the amount of token types.");

	// Let's assume this is never invalid, since nothing generates a TK_MAX.
	return &rules[p_token_type];
}

bool GDScriptParser::SuiteNode::has_local(const StringName &p_name) const {
	if (locals_indices.has(p_name)) {
		return true;
	}
	if (parent_block != nullptr) {
		return parent_block->has_local(p_name);
	}
	return false;
}

const GDScriptParser::SuiteNode::Local &GDScriptParser::SuiteNode::get_local(const StringName &p_name) const {
	if (locals_indices.has(p_name)) {
		return locals[locals_indices[p_name]];
	}
	if (parent_block != nullptr) {
		return parent_block->get_local(p_name);
	}
	return empty;
}

bool GDScriptParser::AnnotationNode::apply(GDScriptParser *p_this, Node *p_target, ClassNode *p_class) {
	if (is_applied) {
		return true;
	}
	is_applied = true;
	if (info == nullptr) {
		// Unresolved custom annotation usage: there is no built-in behavior to apply.
		// The analyzer resolves and validates it separately.
		return true;
	}
	return (p_this->*(p_this->valid_annotations[name].apply))(this, p_target, p_class);
}

bool GDScriptParser::AnnotationNode::applies_to(uint32_t p_target_kinds) const {
	if (info == nullptr) {
		// Unresolved custom annotation usage. The exact declaration is resolved by the analyzer
		// later, but custom annotations only ever target the declaration surface (class, method,
		// member variable, signal, constant), so the parser attaches to those positions and keeps
		// the existing placement diagnostic for anything else (enums, statements, standalone slots,
		// script level, or a pending annotation before an `annotation` declaration).
		const uint32_t custom_targets = AnnotationInfo::CLASS | AnnotationInfo::VARIABLE | AnnotationInfo::FUNCTION | AnnotationInfo::SIGNAL | AnnotationInfo::CONSTANT;
		return (p_target_kinds & custom_targets) != 0;
	}
	return (info->target_kind & p_target_kinds) > 0;
}

bool GDScriptParser::validate_annotation_arguments(AnnotationNode *p_annotation) {
	ERR_FAIL_COND_V_MSG(!valid_annotations.has(p_annotation->name), false, vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	const MethodInfo &info = valid_annotations[p_annotation->name].info;

	if (((info.flags & METHOD_FLAG_VARARG) == 0) && p_annotation->arguments.size() > info.arguments.size()) {
		push_error(vformat(R"(Annotation "%s" requires at most %d arguments, but %d were given.)", p_annotation->name, info.arguments.size(), p_annotation->arguments.size()));
		return false;
	}

	if (p_annotation->arguments.size() < info.arguments.size() - info.default_arguments.size()) {
		push_error(vformat(R"(Annotation "%s" requires at least %d arguments, but %d were given.)", p_annotation->name, info.arguments.size() - info.default_arguments.size(), p_annotation->arguments.size()));
		return false;
	}

	// Some annotations need to be resolved and applied in the parser.
	if (p_annotation->name == SNAME("@icon") || p_annotation->name == SNAME("@warning_ignore_start") || p_annotation->name == SNAME("@warning_ignore_restore")) {
		for (int i = 0; i < p_annotation->arguments.size(); i++) {
			ExpressionNode *argument = p_annotation->arguments[i];

			if (argument->type != Node::LITERAL) {
				push_error(vformat(R"(Argument %d of annotation "%s" must be a string literal.)", i + 1, p_annotation->name), argument);
				return false;
			}

			Variant value = static_cast<LiteralNode *>(argument)->value;

			if (value.get_type() != Variant::STRING) {
				push_error(vformat(R"(Argument %d of annotation "%s" must be a string literal.)", i + 1, p_annotation->name), argument);
				return false;
			}

			p_annotation->resolved_arguments.push_back(value);
		}
	}

	// For other annotations, see `GDScriptAnalyzer::resolve_annotation()`.

	return true;
}

bool GDScriptParser::tool_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	if (_is_tool) {
		push_error(R"("@tool" annotation can only be used once.)", p_annotation);
		return false;
	}
#endif // DEBUG_ENABLED
	_is_tool = true;
	return true;
}

bool GDScriptParser::icon_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::CLASS, false, R"("@icon" annotation can only be applied to classes.)");
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	ClassNode *class_node = static_cast<ClassNode *>(p_target);
	String path = p_annotation->resolved_arguments[0];

#ifdef DEBUG_ENABLED
	if (!class_node->icon_path.is_empty()) {
		push_error(R"("@icon" annotation can only be used once.)", p_annotation);
		return false;
	}
	if (path.is_empty()) {
		push_error(R"("@icon" annotation argument must contain the path to the icon.)", p_annotation->arguments[0]);
		return false;
	}
#endif // DEBUG_ENABLED

	class_node->icon_path = path;

	if (path.is_empty() || path.is_absolute_path()) {
		class_node->simplified_icon_path = path.simplify_path();
	} else if (path.is_relative_path()) {
		class_node->simplified_icon_path = script_path.get_base_dir().path_join(path).simplify_path();
	} else {
		class_node->simplified_icon_path = path;
	}

	return true;
}

bool GDScriptParser::static_unload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::CLASS, false, vformat(R"("%s" annotation can only be applied to classes.)", p_annotation->name));
	ClassNode *class_node = static_cast<ClassNode *>(p_target);
	if (class_node->annotated_static_unload) {
		push_error(vformat(R"("%s" annotation can only be used once per script.)", p_annotation->name), p_annotation);
		return false;
	}
	class_node->annotated_static_unload = true;
	return true;
}

bool GDScriptParser::autoload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(
			p_target->type != Node::CLASS,
			false,
			R"("@autoload" annotation can only be applied to the root script declaration.)");
	return true;
}

bool GDScriptParser::noreturn_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(
			p_target->type != Node::FUNCTION,
			false,
			R"("@noreturn" annotation can only be applied to functions.)");

	FunctionNode *function_node = static_cast<FunctionNode *>(p_target);
	if (function_node->is_noreturn) {
		push_error(R"("@noreturn" annotation can only be used once per function.)", p_annotation);
		return false;
	}
	function_node->is_noreturn = true;
	return true;
}

bool GDScriptParser::onready_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, R"("@onready" annotation can only be applied to class variables.)");

	if (current_class && !ClassDB::is_parent_class(current_class->get_datatype().native_type, SNAME("Node"))) {
		push_error(R"("@onready" can only be used in classes that inherit "Node".)", p_annotation);
		return false;
	}

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(R"("@onready" annotation cannot be applied to a static variable.)", p_annotation);
		return false;
	}
	if (variable->onready) {
		push_error(R"("@onready" annotation can only be used once per variable.)", p_annotation);
		return false;
	}
	variable->onready = true;
	current_class->onready_used = true;
	return true;
}

static String _get_annotation_error_string(const StringName &p_annotation_name, const Vector<Variant::Type> &p_expected_types, const GDScriptParser::DataType &p_provided_type) {
	Vector<String> types;
	for (int i = 0; i < p_expected_types.size(); i++) {
		const Variant::Type &type = p_expected_types[i];
		types.push_back(Variant::get_type_name(type));
		types.push_back("Array[" + Variant::get_type_name(type) + "]");
		switch (type) {
			case Variant::INT:
				types.push_back("PackedByteArray");
				types.push_back("PackedInt32Array");
				types.push_back("PackedInt64Array");
				break;
			case Variant::FLOAT:
				types.push_back("PackedFloat32Array");
				types.push_back("PackedFloat64Array");
				break;
			case Variant::STRING:
				types.push_back("PackedStringArray");
				break;
			case Variant::VECTOR2:
				types.push_back("PackedVector2Array");
				break;
			case Variant::VECTOR3:
				types.push_back("PackedVector3Array");
				break;
			case Variant::COLOR:
				types.push_back("PackedColorArray");
				break;
			case Variant::VECTOR4:
				types.push_back("PackedVector4Array");
				break;
			default:
				break;
		}
	}

	String string;
	if (types.size() == 1) {
		string = types[0].quote();
	} else if (types.size() == 2) {
		string = types[0].quote() + " or " + types[1].quote();
	} else if (types.size() >= 3) {
		string = types[0].quote();
		for (int i = 1; i < types.size() - 1; i++) {
			string += ", " + types[i].quote();
		}
		string += ", or " + types[types.size() - 1].quote();
	}

	return vformat(R"("%s" annotation requires a variable of type %s, but type "%s" was given instead.)", p_annotation_name, string, p_provided_type.to_string());
}

static StringName _find_narrowest_native_or_global_class(const GDScriptParser::DataType &p_type) {
	switch (p_type.kind) {
		case GDScriptParser::DataType::NATIVE: {
			if (p_type.is_meta_type) {
				return Object::get_class_static(); // `GDScriptNativeClass` is not an exposed class.
			}
			return p_type.native_type;
		} break;
		case GDScriptParser::DataType::SCRIPT: {
			Ref<Script> script;
			if (p_type.script_type.is_valid()) {
				script = p_type.script_type;
			} else {
				script = ResourceLoader::load(p_type.script_path, SNAME("Script"));
			}

			if (p_type.is_meta_type) {
				return script.is_valid() ? script->get_class_name() : Script::get_class_static();
			}
			if (script.is_null()) {
				return p_type.native_type;
			}
			if (script->get_global_name() != StringName()) {
				return script->get_global_name();
			}

			Ref<Script> base_script = script->get_base_script();
			if (base_script.is_null()) {
				return script->get_instance_base_type();
			}

			GDScriptParser::DataType base_type;
			base_type.kind = GDScriptParser::DataType::SCRIPT;
			base_type.builtin_type = Variant::OBJECT;
			base_type.native_type = base_script->get_instance_base_type();
			base_type.script_type = base_script;
			base_type.script_path = base_script->get_path();

			return _find_narrowest_native_or_global_class(base_type);
		} break;
		case GDScriptParser::DataType::CLASS: {
			if (p_type.is_meta_type) {
				return GDScript::get_class_static();
			}
			if (p_type.class_type == nullptr) {
				return p_type.native_type;
			}
			if (p_type.class_type->get_global_name() != StringName()) {
				return p_type.class_type->get_global_name();
			}
			return _find_narrowest_native_or_global_class(p_type.class_type->base_type);
		} break;
		default: {
			ERR_FAIL_V(StringName());
		} break;
	}
}

template <PropertyHint t_hint, Variant::Type t_type>
bool GDScriptParser::export_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_NULL_V(p_class, false);

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	variable->export_info.type = t_type;
	variable->export_info.hint = t_hint;

	String hint_string;
	for (int i = 0; i < p_annotation->resolved_arguments.size(); i++) {
		String arg_string = String(p_annotation->resolved_arguments[i]);

		if (p_annotation->name != SNAME("@export_placeholder")) {
			if (arg_string.is_empty()) {
				push_error(vformat(R"(Argument %d of annotation "%s" is empty.)", i + 1, p_annotation->name), p_annotation->arguments[i]);
				return false;
			}
			if (arg_string.contains_char(',')) {
				push_error(vformat(R"(Argument %d of annotation "%s" contains a comma. Use separate arguments instead.)", i + 1, p_annotation->name), p_annotation->arguments[i]);
				return false;
			}
		}

		// WARNING: Do not merge with the previous `if` because there `!=`, not `==`!
		if (p_annotation->name == SNAME("@export_flags")) {
			const int64_t max_flags = 32;
			Vector<String> t = arg_string.split(":", true, 1);
			if (t[0].is_empty()) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Expected flag name.)", i + 1), p_annotation->arguments[i]);
				return false;
			}
			if (t.size() == 2) {
				if (t[1].is_empty()) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Expected flag value.)", i + 1), p_annotation->arguments[i]);
					return false;
				}
				if (!t[1].is_valid_int()) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": The flag value must be a valid integer.)", i + 1), p_annotation->arguments[i]);
					return false;
				}
				int64_t value = t[1].to_int();
				if (value < 1 || value >= (1LL << max_flags)) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": The flag value must be at least 1 and at most 2 ** %d - 1.)", i + 1, max_flags), p_annotation->arguments[i]);
					return false;
				}
			} else if (i >= max_flags) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Starting from argument %d, the flag value must be specified explicitly.)", i + 1, max_flags + 1), p_annotation->arguments[i]);
				return false;
			}
		} else if (p_annotation->name == SNAME("@export_node_path")) {
			String native_class = arg_string;
			if (ScriptServer::is_global_class(arg_string)) {
				native_class = ScriptServer::get_global_class_native_base(arg_string);
			}
			if (!ClassDB::class_exists(native_class) || !ClassDB::is_class_exposed(native_class)) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_node_path": The class "%s" was not found in the global scope.)", i + 1, arg_string), p_annotation->arguments[i]);
				return false;
			} else if (!ClassDB::is_parent_class(native_class, SNAME("Node"))) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_node_path": The class "%s" does not inherit "Node".)", i + 1, arg_string), p_annotation->arguments[i]);
				return false;
			}
		}

		if (i > 0) {
			hint_string += ",";
		}
		hint_string += arg_string;
	}
	variable->export_info.hint_string = hint_string;

	// This is called after the analyzer is done finding the type, so this should be set here.
	DataType export_type = variable->get_datatype();

	// Use initializer type if specified type is `Variant`.
	if (export_type.is_variant() && variable->initializer != nullptr && variable->initializer->datatype.is_set()) {
		export_type = variable->initializer->get_datatype();
		export_type.type_source = DataType::INFERRED;
	}

	const Variant::Type original_export_type_builtin = export_type.builtin_type;

	// Process array and packed array annotations on the element type.
	bool is_array = false;
	if (export_type.builtin_type == Variant::ARRAY && export_type.has_container_element_type(0)) {
		is_array = true;
		export_type = export_type.get_container_element_type(0);
	} else if (export_type.is_typed_container_type()) {
		is_array = true;
		export_type = export_type.get_typed_container_type();
		export_type.type_source = variable->datatype.type_source;
	}

	bool is_dict = false;
	if (export_type.builtin_type == Variant::DICTIONARY && export_type.has_container_element_types()) {
		is_dict = true;
		DataType inner_type = export_type.get_container_element_type_or_variant(1);
		export_type = export_type.get_container_element_type_or_variant(0);
		export_type.set_container_element_type(0, inner_type); // Store earlier extracted value within key to separately parse after.
	}

	bool use_default_variable_type_check = true;

	if (p_annotation->name == SNAME("@export_range")) {
		if (export_type.builtin_type == Variant::INT) {
			variable->export_info.type = Variant::INT;
		}
	} else if (p_annotation->name == SNAME("@export_multiline")) {
		use_default_variable_type_check = false;

		if (export_type.builtin_type != Variant::STRING && export_type.builtin_type != Variant::DICTIONARY) {
			Vector<Variant::Type> expected_types = { Variant::STRING, Variant::DICTIONARY };
			push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
			return false;
		}

		if (export_type.builtin_type == Variant::DICTIONARY) {
			variable->export_info.type = Variant::DICTIONARY;
		}
	} else if (p_annotation->name == SNAME("@export")) {
		use_default_variable_type_check = false;

		if (variable->datatype_specifier == nullptr && variable->initializer == nullptr) {
			push_error(R"(Cannot use simple "@export" annotation with variable without type or initializer, since type can't be inferred.)", p_annotation);
			return false;
		}

		if (export_type.has_no_type()) {
			push_error(R"(Cannot use simple "@export" annotation because the type of the initialized value can't be inferred.)", p_annotation);
			return false;
		}

		switch (export_type.kind) {
			case GDScriptParser::DataType::BUILTIN:
				variable->export_info.type = export_type.builtin_type;
				variable->export_info.hint = PROPERTY_HINT_NONE;
				variable->export_info.hint_string = String();
				break;
			case GDScriptParser::DataType::NATIVE:
			case GDScriptParser::DataType::SCRIPT:
			case GDScriptParser::DataType::CLASS: {
				const StringName class_name = _find_narrowest_native_or_global_class(export_type);
				if (ClassDB::is_parent_class(export_type.native_type, SNAME("Resource"))) {
					variable->export_info.type = Variant::OBJECT;
					variable->export_info.hint = PROPERTY_HINT_RESOURCE_TYPE;
					variable->export_info.hint_string = class_name;
				} else if (ClassDB::is_parent_class(export_type.native_type, SNAME("Node"))) {
					variable->export_info.type = Variant::OBJECT;
					variable->export_info.hint = PROPERTY_HINT_NODE_TYPE;
					variable->export_info.hint_string = class_name;
				} else {
					push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
					return false;
				}
			} break;
			case GDScriptParser::DataType::ENUM: {
				if (export_type.is_meta_type) {
					variable->export_info.type = Variant::DICTIONARY;
				} else {
					variable->export_info.type = Variant::INT;
					variable->export_info.hint = PROPERTY_HINT_ENUM;

					String enum_hint_string;
					bool first = true;
					for (const KeyValue<StringName, int64_t> &E : export_type.enum_values) {
						if (!first) {
							enum_hint_string += ",";
						} else {
							first = false;
						}
						enum_hint_string += E.key.operator String().capitalize().xml_escape();
						enum_hint_string += ":";
						enum_hint_string += String::num_int64(E.value).xml_escape();
					}

					variable->export_info.hint_string = enum_hint_string;
					variable->export_info.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
					variable->export_info.class_name = String(export_type.native_type).replace("::", ".");
				}
			} break;
			case GDScriptParser::DataType::VARIANT: {
				if (export_type.is_variant()) {
					variable->export_info.type = Variant::NIL;
					variable->export_info.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
				}
			} break;
			default:
				push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
				return false;
		}

		if (variable->export_info.hint == PROPERTY_HINT_NODE_TYPE && !ClassDB::is_parent_class(p_class->base_type.native_type, SNAME("Node"))) {
			push_error(vformat(R"(Node export is only supported in Node-derived classes, but the current class inherits "%s".)", p_class->base_type.to_string()), p_annotation);
			return false;
		}

		if (is_dict) {
			String key_prefix = itos(variable->export_info.type);
			if (variable->export_info.hint) {
				key_prefix += "/" + itos(variable->export_info.hint);
			}
			key_prefix += ":" + variable->export_info.hint_string;

			// Now parse value.
			export_type = export_type.get_container_element_type(0);

			if (export_type.is_variant() || export_type.has_no_type()) {
				export_type.kind = GDScriptParser::DataType::BUILTIN;
			}
			switch (export_type.kind) {
				case GDScriptParser::DataType::BUILTIN:
					variable->export_info.type = export_type.builtin_type;
					variable->export_info.hint = PROPERTY_HINT_NONE;
					variable->export_info.hint_string = String();
					break;
				case GDScriptParser::DataType::NATIVE:
				case GDScriptParser::DataType::SCRIPT:
				case GDScriptParser::DataType::CLASS: {
					const StringName class_name = _find_narrowest_native_or_global_class(export_type);
					if (ClassDB::is_parent_class(export_type.native_type, SNAME("Resource"))) {
						variable->export_info.type = Variant::OBJECT;
						variable->export_info.hint = PROPERTY_HINT_RESOURCE_TYPE;
						variable->export_info.hint_string = class_name;
					} else if (ClassDB::is_parent_class(export_type.native_type, SNAME("Node"))) {
						variable->export_info.type = Variant::OBJECT;
						variable->export_info.hint = PROPERTY_HINT_NODE_TYPE;
						variable->export_info.hint_string = class_name;
					} else {
						push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
						return false;
					}
				} break;
				case GDScriptParser::DataType::ENUM: {
					if (export_type.is_meta_type) {
						variable->export_info.type = Variant::DICTIONARY;
					} else {
						variable->export_info.type = Variant::INT;
						variable->export_info.hint = PROPERTY_HINT_ENUM;

						String enum_hint_string;
						bool first = true;
						for (const KeyValue<StringName, int64_t> &E : export_type.enum_values) {
							if (!first) {
								enum_hint_string += ",";
							} else {
								first = false;
							}
							enum_hint_string += E.key.operator String().capitalize().xml_escape();
							enum_hint_string += ":";
							enum_hint_string += String::num_int64(E.value).xml_escape();
						}

						variable->export_info.hint_string = enum_hint_string;
						variable->export_info.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
						variable->export_info.class_name = String(export_type.native_type).replace("::", ".");
					}
				} break;
				default:
					push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
					return false;
			}

			if (variable->export_info.hint == PROPERTY_HINT_NODE_TYPE && !ClassDB::is_parent_class(p_class->base_type.native_type, SNAME("Node"))) {
				push_error(vformat(R"(Node export is only supported in Node-derived classes, but the current class inherits "%s".)", p_class->base_type.to_string()), p_annotation);
				return false;
			}

			String value_prefix = itos(variable->export_info.type);
			if (variable->export_info.hint) {
				value_prefix += "/" + itos(variable->export_info.hint);
			}
			value_prefix += ":" + variable->export_info.hint_string;

			variable->export_info.type = Variant::DICTIONARY;
			variable->export_info.hint = PROPERTY_HINT_TYPE_STRING;
			variable->export_info.hint_string = key_prefix + ";" + value_prefix;
			variable->export_info.usage = PROPERTY_USAGE_DEFAULT;
			variable->export_info.class_name = StringName();
		}
	} else if (p_annotation->name == SNAME("@export_enum")) {
		use_default_variable_type_check = false;

		Variant::Type enum_type = Variant::INT;

		if (export_type.kind == DataType::BUILTIN && export_type.builtin_type == Variant::STRING) {
			enum_type = Variant::STRING;
		}

		variable->export_info.type = enum_type;

		if (!export_type.is_variant() && (export_type.kind != DataType::BUILTIN || export_type.builtin_type != enum_type)) {
			Vector<Variant::Type> expected_types = { Variant::INT, Variant::STRING };
			push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
			return false;
		}
	}

	if (use_default_variable_type_check) {
		// Validate variable type with export.
		if (!export_type.is_variant() && (export_type.kind != DataType::BUILTIN || export_type.builtin_type != t_type)) {
			// Allow float/int conversion.
			if ((t_type != Variant::FLOAT || export_type.builtin_type != Variant::INT) && (t_type != Variant::INT || export_type.builtin_type != Variant::FLOAT)) {
				Vector<Variant::Type> expected_types = { t_type };
				push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
				return false;
			}
		}
	}

	if (is_array) {
		String hint_prefix = itos(variable->export_info.type);
		if (variable->export_info.hint) {
			hint_prefix += "/" + itos(variable->export_info.hint);
		}
		variable->export_info.type = original_export_type_builtin;
		variable->export_info.hint = PROPERTY_HINT_TYPE_STRING;
		variable->export_info.hint_string = hint_prefix + ":" + variable->export_info.hint_string;
		variable->export_info.usage = PROPERTY_USAGE_DEFAULT;
		variable->export_info.class_name = StringName();
	}

	return true;
}

// For `@export_storage` and `@export_custom`, there is no need to check the variable type, argument values,
// or handle array exports in a special way, so they are implemented as separate methods.

bool GDScriptParser::export_storage_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	// Save the info because the compiler uses export info for overwriting member info.
	variable->export_info = variable->get_datatype().to_property_info(variable->identifier->name);
	variable->export_info.usage |= PROPERTY_USAGE_STORAGE;

	return true;
}

bool GDScriptParser::export_custom_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_COND_V_MSG(p_annotation->resolved_arguments.size() < 2, false, R"(Annotation "@export_custom" requires 2 arguments.)");

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	DataType export_type = variable->get_datatype();

	variable->export_info.type = export_type.builtin_type;
	variable->export_info.hint = static_cast<PropertyHint>(p_annotation->resolved_arguments[0].operator int64_t());
	variable->export_info.hint_string = p_annotation->resolved_arguments[1];

	if (p_annotation->resolved_arguments.size() >= 3) {
		variable->export_info.usage = p_annotation->resolved_arguments[2].operator int64_t();
	}
	return true;
}

bool GDScriptParser::export_tool_button_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef TOOLS_ENABLED
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	if (!is_tool()) {
		push_error(R"(Tool buttons can only be used in tool scripts (add "@tool" to the top of the script).)", p_annotation);
		return false;
	}

	VariableNode *variable = static_cast<VariableNode *>(p_target);

	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	const DataType variable_type = variable->get_datatype();
	if (!variable_type.is_variant() && variable_type.is_hard_type()) {
		if (variable_type.kind != DataType::BUILTIN || variable_type.builtin_type != Variant::CALLABLE) {
			push_error(vformat(R"("@export_tool_button" annotation requires a variable of type "Callable", but type "%s" was given instead.)", variable_type.to_string()), p_annotation);
			return false;
		}
	}

	variable->exported = true;

	// Build the hint string (format: `<text>[,<icon>]`).
	String hint_string = p_annotation->resolved_arguments[0].operator String(); // Button text.
	if (p_annotation->resolved_arguments.size() > 1) {
		hint_string += "," + p_annotation->resolved_arguments[1].operator String(); // Button icon.
	}

	variable->export_info.type = Variant::CALLABLE;
	variable->export_info.hint = PROPERTY_HINT_TOOL_BUTTON;
	variable->export_info.hint_string = hint_string;
	variable->export_info.usage = PROPERTY_USAGE_EDITOR;
#endif // TOOLS_ENABLED

	return true; // Only available in editor.
}

template <PropertyUsageFlags t_usage>
bool GDScriptParser::export_group_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	p_annotation->export_info.name = p_annotation->resolved_arguments[0];

	switch (t_usage) {
		case PROPERTY_USAGE_CATEGORY: {
			p_annotation->export_info.usage = t_usage;
		} break;

		case PROPERTY_USAGE_GROUP: {
			p_annotation->export_info.usage = t_usage;
			if (p_annotation->resolved_arguments.size() == 2) {
				p_annotation->export_info.hint_string = p_annotation->resolved_arguments[1];
			}
		} break;

		case PROPERTY_USAGE_SUBGROUP: {
			p_annotation->export_info.usage = t_usage;
			if (p_annotation->resolved_arguments.size() == 2) {
				p_annotation->export_info.hint_string = p_annotation->resolved_arguments[1];
			}
		} break;
	}

	return true;
}

bool GDScriptParser::warning_ignore_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	bool has_error = false;
	for (const Variant &warning_name : p_annotation->resolved_arguments) {
		GDScriptWarning::Code warning_code = GDScriptWarning::get_code_from_name(String(warning_name).to_upper());
		if (warning_code == GDScriptWarning::WARNING_MAX) {
			push_error(vformat(R"(Invalid warning name: "%s".)", warning_name), p_annotation);
			has_error = true;
		} else {
			int start_line = p_annotation->start_line;
			int end_line = p_target->end_line;

			switch (p_target->type) {
#define SIMPLE_CASE(m_type, m_class, m_property)          \
	case m_type: {                                        \
		m_class *node = static_cast<m_class *>(p_target); \
		if (node->m_property == nullptr) {                \
			end_line = node->start_line;                  \
		} else {                                          \
			end_line = node->m_property->end_line;        \
		}                                                 \
	} break;

				// Can contain properties (set/get).
				SIMPLE_CASE(Node::VARIABLE, VariableNode, initializer)
#undef SIMPLE_CASE

				case Node::CLASS: {
					end_line = p_target->start_line;
					for (const AnnotationNode *annotation : p_target->annotations) {
						start_line = MIN(start_line, annotation->start_line);
						end_line = MAX(end_line, annotation->end_line);
					}
				} break;

				case Node::FUNCTION: {
					FunctionNode *function = static_cast<FunctionNode *>(p_target);
					end_line = function->start_line;
					for (int i = 0; i < function->parameters.size(); i++) {
						end_line = MAX(end_line, function->parameters[i]->end_line);
						if (function->parameters[i]->initializer != nullptr) {
							end_line = MAX(end_line, function->parameters[i]->initializer->end_line);
						}
					}
				} break;

				case Node::MATCH_BRANCH: {
					MatchBranchNode *branch = static_cast<MatchBranchNode *>(p_target);
					end_line = branch->start_line;
					for (int i = 0; i < branch->patterns.size(); i++) {
						end_line = MAX(end_line, branch->patterns[i]->end_line);
					}
				} break;

				default: {
				} break;
			}

			end_line = MAX(start_line, end_line); // Prevent infinite loop.
			for (int line = start_line; line <= end_line; line++) {
				warning_ignored_lines[warning_code].insert(line);
			}
		}
	}
	return !has_error;
#else // !DEBUG_ENABLED
	// Only available in debug builds.
	return true;
#endif // DEBUG_ENABLED
}

bool GDScriptParser::warning_ignore_region_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	bool has_error = false;
	const bool is_start = p_annotation->name == SNAME("@warning_ignore_start");
	for (const Variant &warning_name : p_annotation->resolved_arguments) {
		GDScriptWarning::Code warning_code = GDScriptWarning::get_code_from_name(String(warning_name).to_upper());
		if (warning_code == GDScriptWarning::WARNING_MAX) {
			push_error(vformat(R"(Invalid warning name: "%s".)", warning_name), p_annotation);
			has_error = true;
			continue;
		}
		if (is_start) {
			if (warning_ignore_start_lines[warning_code] != INT_MAX) {
				push_error(vformat(R"(Warning "%s" is already being ignored by "@warning_ignore_start" at line %d.)", String(warning_name).to_upper(), warning_ignore_start_lines[warning_code]), p_annotation);
				has_error = true;
				continue;
			}
			warning_ignore_start_lines[warning_code] = p_annotation->start_line;
		} else {
			if (warning_ignore_start_lines[warning_code] == INT_MAX) {
				push_error(vformat(R"(Warning "%s" is not being ignored by "@warning_ignore_start".)", String(warning_name).to_upper()), p_annotation);
				has_error = true;
				continue;
			}
			const int start_line = warning_ignore_start_lines[warning_code];
			const int end_line = MAX(start_line, p_annotation->start_line); // Prevent infinite loop.
			for (int i = start_line; i <= end_line; i++) {
				warning_ignored_lines[warning_code].insert(i);
			}
			warning_ignore_start_lines[warning_code] = INT_MAX;
		}
	}
	return !has_error;
#else // !DEBUG_ENABLED
	// Only available in debug builds.
	return true;
#endif // DEBUG_ENABLED
}

bool GDScriptParser::rpc_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::FUNCTION, false, vformat(R"("%s" annotation can only be applied to functions.)", p_annotation->name));

	FunctionNode *function = static_cast<FunctionNode *>(p_target);
	if (function->rpc_config.get_type() != Variant::NIL) {
		push_error(R"(RPC annotations can only be used once per function.)", p_annotation);
		return false;
	}

	// Default values should match the annotation registration defaults and `SceneRPCInterface::_parse_rpc_config()`.
	Dictionary rpc_config;
	rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
	if (!p_annotation->resolved_arguments.is_empty()) {
		unsigned char locality_args = 0;
		unsigned char permission_args = 0;
		unsigned char transfer_mode_args = 0;

		for (int i = 0; i < p_annotation->resolved_arguments.size(); i++) {
			if (i == 3) {
				rpc_config["channel"] = p_annotation->resolved_arguments[i].operator int();
				continue;
			}

			String arg = p_annotation->resolved_arguments[i].operator String();
			if (arg == "call_local") {
				locality_args++;
				rpc_config["call_local"] = true;
			} else if (arg == "call_remote") {
				locality_args++;
				rpc_config["call_local"] = false;
			} else if (arg == "any_peer") {
				permission_args++;
				rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
			} else if (arg == "authority") {
				permission_args++;
				rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
			} else if (arg == "reliable") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
			} else if (arg == "unreliable") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
			} else if (arg == "unreliable_ordered") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED;
			} else {
				push_error(R"(Invalid RPC argument. Must be one of: "call_local"/"call_remote" (local calls), "any_peer"/"authority" (permission), "reliable"/"unreliable"/"unreliable_ordered" (transfer mode).)", p_annotation);
			}
		}

		if (locality_args > 1) {
			push_error(R"(Invalid RPC config. The locality ("call_local"/"call_remote") must be specified no more than once.)", p_annotation);
		} else if (permission_args > 1) {
			push_error(R"(Invalid RPC config. The permission ("any_peer"/"authority") must be specified no more than once.)", p_annotation);
		} else if (transfer_mode_args > 1) {
			push_error(R"(Invalid RPC config. The transfer mode ("reliable"/"unreliable"/"unreliable_ordered") must be specified no more than once.)", p_annotation);
		}
	}
	function->rpc_config = rpc_config;
	return true;
}

GDScriptParser::DataType GDScriptParser::SuiteNode::Local::get_datatype() const {
	switch (type) {
		case CONSTANT:
			return constant->get_datatype();
		case VARIABLE:
			return variable->get_datatype();
		case PARAMETER:
			return parameter->get_datatype();
		case FOR_VARIABLE:
		case PATTERN_BIND:
			return bind->get_datatype();
		case UNDEFINED:
			return DataType();
	}
	return DataType();
}

String GDScriptParser::SuiteNode::Local::get_name() const {
	switch (type) {
		case SuiteNode::Local::PARAMETER:
			return "parameter";
		case SuiteNode::Local::CONSTANT:
			return "constant";
		case SuiteNode::Local::VARIABLE:
			return "variable";
		case SuiteNode::Local::FOR_VARIABLE:
			return "for loop iterator";
		case SuiteNode::Local::PATTERN_BIND:
			return "pattern bind";
		case SuiteNode::Local::UNDEFINED:
			return "<undefined>";
		default:
			return String();
	}
}

static String _datatype_signature_type_to_string(const GDScriptParser::DataType &p_type, bool p_nil_is_void = false) {
	if (p_nil_is_void && p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL) {
		return "void";
	}
	return p_type.to_string();
}

static String _method_signature_to_string(const Vector<GDScriptParser::DataType> &p_argument_types, const Vector<GDScriptParser::DataType> &p_return_type, bool p_has_return) {
	Vector<String> argument_types;
	for (const GDScriptParser::DataType &argument_type : p_argument_types) {
		argument_types.append(_datatype_signature_type_to_string(argument_type));
	}

	const String arguments = String(", ").join(argument_types);
	if (p_has_return) {
		const String return_type = p_return_type.is_empty() ? "void" : _datatype_signature_type_to_string(p_return_type[0], true);
		return vformat("[[%s], %s]", arguments, return_type);
	}
	return vformat("[[%s]]", arguments);
}

String GDScriptParser::DataType::to_string() const {
	if (is_type_handle_annotation) {
		DataType represented_type = *this;
		represented_type.is_meta_type = false;
		represented_type.is_type_handle_annotation = false;
		represented_type.is_constant = false;
		represented_type.is_nullable = false;
		const String nullable_suffix = is_nullable ? "?" : "";
		return vformat("Type[%s]%s", represented_type.to_string(), nullable_suffix);
	}

	if (is_coroutine) {
		// Coroutine[T] is a source-level skin over GDScriptFunctionState. Render the phantom result
		// type from container_element_types[0] as the only choke point, so the underlying native
		// class name never leaks into hovers, errors, or completion. A void result (NIL) and an
		// absent result render with their source spellings.
		String element = "Variant";
		if (has_container_element_type(0)) {
			const DataType result_type = get_container_element_type(0);
			if (result_type.kind == BUILTIN && result_type.builtin_type == Variant::NIL) {
				element = "void";
			} else {
				element = result_type.to_string();
			}
		}
		const String nullable_suffix = is_nullable ? "?" : "";
		return vformat("Coroutine[%s]%s", element, nullable_suffix);
	}

	String result;
	bool valid_kind = true;
	switch (kind) {
		case VARIANT:
			result = "Variant";
			break;
		case BUILTIN:
			if (builtin_type == Variant::NIL) {
				return "null";
			}
			if (builtin_type == Variant::CALLABLE && has_explicit_method_signature) {
				const char *callable_name = signature_is_async ? "AsyncCallable" : "Callable";
				result = vformat("%s%s", callable_name, _method_signature_to_string(method_parameter_types, method_return_type, true));
				break;
			}
			if (builtin_type == Variant::CALLABLE && signature_is_async) {
				// A bare AsyncCallable (or a reference to an async method) carries the async marker
				// without an explicit signature; still render it as AsyncCallable for readability.
				result = "AsyncCallable";
				break;
			}
			if (builtin_type == Variant::SIGNAL && has_explicit_method_signature) {
				result = vformat("Signal%s", _method_signature_to_string(method_parameter_types, method_return_type, false));
				break;
			}
			if (builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				result = vformat("Array[%s]", get_container_element_type(0).to_string());
				break;
			}
			if (builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				result = vformat("Dictionary[%s, %s]", get_container_element_type_or_variant(0).to_string(), get_container_element_type_or_variant(1).to_string());
				break;
			}
			result = Variant::get_type_name(builtin_type);
			break;
		case NATIVE:
			if (is_meta_type) {
				result = GDScriptNativeClass::get_class_static();
				break;
			}
			result = native_type.operator String();
			break;
		case CLASS:
			if (class_type->identifier != nullptr) {
				result = class_type->identifier->name.operator String();
				break;
			}
			result = class_type->fqcn;
			break;
		case SCRIPT: {
			if (is_meta_type) {
				result = script_type.is_valid() ? script_type->get_class_name().operator String() : "";
				break;
			}
			String name = script_type.is_valid() ? script_type->get_name() : "";
			if (!name.is_empty()) {
				result = name;
				break;
			}
			name = script_path;
			if (!name.is_empty()) {
				result = name;
				break;
			}
			result = native_type.operator String();
			break;
		}
		case ENUM: {
			// native_type contains either the native class defining the enum
			// or the fully qualified class name of the script defining the enum
			result = String(native_type).get_file(); // Remove path, keep filename
			break;
		}
		case TYPE_PARAMETER:
			result = type_parameter_name == SNAME("@Self") ? "Self" : type_parameter_name.operator String();
			break;
		case RESOLVING:
		case UNRESOLVED:
			result = "<unresolved type>";
			break;
		default:
			valid_kind = false;
			break;
	}

	if (!valid_kind) {
		ERR_FAIL_V_MSG("<unresolved type>", "Kind set outside the enum range.");
	}

	// Render specialized type arguments, e.g. Box[int] or Box[String, float].
	if (kind != TYPE_PARAMETER && !type_arguments.is_empty()) {
		String arguments;
		for (int i = 0; i < type_arguments.size(); i++) {
			if (i > 0) {
				arguments += ", ";
			}
			arguments += type_arguments[i].to_string();
		}
		result += vformat("[%s]", arguments);
	}
	if (is_nullable && kind != VARIANT && !(kind == BUILTIN && builtin_type == Variant::NIL)) {
		result += "?";
	}
	return result;
}

GDScriptParser::DataType GDScriptParser::DataType::substitute(const DataType &p_type, const HashMap<StringName, DataType> &p_bindings) {
	if (p_type.kind == TYPE_PARAMETER) {
		const DataType *binding = p_bindings.getptr(p_type.type_parameter_name);
		if (binding != nullptr) {
			DataType result = *binding;
			if (p_type.is_type_handle_annotation) {
				result.is_meta_type = true;
				result.is_type_handle_annotation = true;
				result.is_pseudo_type = false;
				result.is_constant = p_type.is_constant;
				result.is_nullable = p_type.is_nullable;
			} else {
				result.is_nullable = result.is_nullable || p_type.is_nullable;
			}
			return result;
		}
		// Unbound parameter: leave it intact so an outer scope can substitute it later, but specialize
		// its bound so a bound referencing a substituted parameter (e.g. `[U: T]` with `T := int`)
		// reflects the binding.
		DataType result = p_type;
		for (int i = 0; i < result.type_parameter_bound.size(); i++) {
			result.type_parameter_bound.write[i] = substitute(result.type_parameter_bound[i], p_bindings);
		}
		return result;
	}

	DataType result = p_type;
	for (int i = 0; i < result.container_element_types.size(); i++) {
		result.container_element_types.write[i] = substitute(result.container_element_types[i], p_bindings);
	}
	for (int i = 0; i < result.type_arguments.size(); i++) {
		result.type_arguments.write[i] = substitute(result.type_arguments[i], p_bindings);
	}
	for (int i = 0; i < result.method_parameter_types.size(); i++) {
		result.method_parameter_types.write[i] = substitute(result.method_parameter_types[i], p_bindings);
	}
	for (int i = 0; i < result.method_return_type.size(); i++) {
		result.method_return_type.write[i] = substitute(result.method_return_type[i], p_bindings);
	}
	return result;
}

// Renders a DataType into the flat hint grammar used by PROPERTY_HINT_CALLABLE_TYPE.
// Mirrors DataType::to_string surface syntax, but leaf script/class/enum names use the same
// name selection as PROPERTY_HINT_ARRAY_TYPE so the result round-trips through type_from_property.
static String _encode_signature_leaf_name(const GDScriptParser::DataType &p_type) {
	switch (p_type.kind) {
		case GDScriptParser::DataType::BUILTIN:
			return Variant::get_type_name(p_type.builtin_type);
		case GDScriptParser::DataType::NATIVE:
			return p_type.native_type;
		case GDScriptParser::DataType::SCRIPT:
			if (p_type.script_type.is_valid() && p_type.script_type->get_global_name() != StringName()) {
				return p_type.script_type->get_global_name();
			}
			return p_type.native_type;
		case GDScriptParser::DataType::CLASS:
			if (p_type.class_type != nullptr && p_type.class_type->get_global_name() != StringName()) {
				return p_type.class_type->get_global_name();
			}
			return p_type.native_type;
		case GDScriptParser::DataType::ENUM:
			return String(p_type.native_type).replace("::", ".");
		default:
			return "Variant";
	}
}

static String _encode_signature_type(const GDScriptParser::DataType &p_type);

// Encodes the phantom result type T of a Coroutine[T] into the flat hint grammar. The bracketed result
// slot mirrors to_string: an absent slot renders as "Variant" and a NIL (void) result as "void", so the
// coroutine round-trips with an awaitable result type. Callers wrap this as "Coroutine[<element>]".
static String _encode_coroutine_result_element(const GDScriptParser::DataType &p_coroutine) {
	if (!p_coroutine.has_container_element_type(0)) {
		return "Variant";
	}
	const GDScriptParser::DataType result_type = p_coroutine.get_container_element_type(0);
	if (result_type.kind == GDScriptParser::DataType::BUILTIN && result_type.builtin_type == Variant::NIL) {
		return "void";
	}
	return _encode_signature_type(result_type);
}

// Encodes a Callable/Signal signature suffix: "[[p0, p1], ret]" for callables, "[[p0, p1]]" for signals.
static String _encode_method_signature_suffix(const GDScriptParser::DataType &p_type, bool p_has_return) {
	Vector<String> params;
	for (const GDScriptParser::DataType &param : p_type.method_parameter_types) {
		params.push_back(_encode_signature_type(param));
	}
	const String joined = String(", ").join(params);
	if (p_has_return) {
		String return_name;
		if (p_type.method_return_type.is_empty()) {
			return_name = "void";
		} else {
			const GDScriptParser::DataType return_type = p_type.method_return_type[0];
			if (return_type.kind == GDScriptParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
				return_name = "void";
			} else {
				return_name = _encode_signature_type(return_type);
			}
		}
		return vformat("[[%s], %s]", joined, return_name);
	}
	return vformat("[[%s]]", joined);
}

static String _encode_signature_type_base(const GDScriptParser::DataType &p_type) {
	// Coroutine[T] is a NATIVE skin over GDScriptFunctionState; intercept it before the leaf fallthrough
	// so the bare native name never leaks and the result type T survives the boundary.
	if (p_type.is_coroutine) {
		return vformat("Coroutine[%s]", _encode_coroutine_result_element(p_type));
	}
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::ARRAY:
				if (p_type.has_container_element_type(0)) {
					return vformat("Array[%s]", _encode_signature_type(p_type.get_container_element_type(0)));
				}
				return "Array";
			case Variant::DICTIONARY:
				if (p_type.has_container_element_types()) {
					return vformat("Dictionary[%s, %s]",
							_encode_signature_type(p_type.get_container_element_type_or_variant(0)),
							_encode_signature_type(p_type.get_container_element_type_or_variant(1)));
				}
				return "Dictionary";
			case Variant::CALLABLE: {
				// AsyncCallable is encoded under a distinct type name so the async marker survives the
				// hint-string round-trip and a cross-script async callable stays distinct from a plain
				// (synchronous) Callable. The decoder rebuilds the marker from this name.
				const String callable_name = p_type.signature_is_async ? "AsyncCallable" : "Callable";
				if (p_type.has_explicit_method_signature) {
					return callable_name + _encode_method_signature_suffix(p_type, true);
				}
				return callable_name;
			}
			case Variant::SIGNAL:
				if (p_type.has_explicit_method_signature) {
					return "Signal" + _encode_method_signature_suffix(p_type, false);
				}
				return "Signal";
			default:
				return Variant::get_type_name(p_type.builtin_type);
		}
	}
	return _encode_signature_leaf_name(p_type);
}

static String _encode_signature_type(const GDScriptParser::DataType &p_type) {
	String encoded = _encode_signature_type_base(p_type);
	// Preserve the nullable marker so a `T?` slot survives the boundary (mirrors to_string's guard:
	// Variant and the NIL builtin are never marked nullable). Generic type_arguments are not yet
	// encoded; a nested user-generic slot degrades to its bare name (tracked as a follow-up).
	if (p_type.is_nullable && p_type.kind != GDScriptParser::DataType::VARIANT &&
			!(p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL)) {
		encoded += "?";
	}
	return encoded;
}

// Whether an enum hint leaf reconstructs to an identical enum identity on the decode side. The leaf is
// "Base.Member" for native-class/built-in enums and a bare name for global enums. This predicate MUST
// stay in sync with the decoder's `_resolve_hint_enum_leaf` (gdscript_analyzer.cpp): the encoder only
// emits an enum hint the decoder can rebuild exactly, so a round-tripped enum slot is never turned into
// a false strict mismatch. Global (CoreConstants), native-class (ClassDB), and built-in (Variant) enums
// qualify; a script/class enum has no stable global name in the grammar and must cross untyped.
static bool _enum_signature_leaf_round_trips(const String &p_name) {
	if (CoreConstants::is_global_enum(p_name)) {
		return true;
	}
	const int separator = p_name.rfind(".");
	if (separator <= 0 || separator >= p_name.length() - 1) {
		return false;
	}
	const String base = p_name.substr(0, separator);
	const StringName enum_name = p_name.substr(separator + 1);
	if (ClassDB::class_exists(base) && ClassDB::has_enum(base, enum_name)) {
		return true;
	}
	const Variant::Type base_builtin = GDScriptParser::get_builtin_type(base);
	return base_builtin < Variant::VARIANT_MAX && Variant::has_enum(base_builtin, enum_name);
}

// True when a signature slot round-trips faithfully through the PROPERTY_HINT_CALLABLE_TYPE decoder.
// Slots that cannot (script/class enum leaves and non-global/nested script-classes reduced to a native
// fallback, unexposed natives, generic type_arguments, type parameters) would decode to a coarser type —
// emitting the hint anyway turns a previously gradual-accepted cross-script callable into a false strict
// mismatch. When any slot is lossy the caller omits the hint, so the callable/signal crosses the boundary
// untyped. Global, native-class, and built-in enum leaves do round-trip (see `_enum_signature_leaf_round_trips`).
static bool _signature_type_is_encodable(const GDScriptParser::DataType &p_type) {
	if (!p_type.type_arguments.is_empty()) {
		return false;
	}
	// Coroutine[T] round-trips as long as its result type T does; the coroutine skin itself is always
	// expressible in the grammar (see _encode_signature_type_base). An absent result is Variant.
	if (p_type.is_coroutine) {
		return !p_type.has_container_element_type(0) || _signature_type_is_encodable(p_type.get_container_element_type(0));
	}
	switch (p_type.kind) {
		case GDScriptParser::DataType::VARIANT:
			return true;
		case GDScriptParser::DataType::BUILTIN:
			switch (p_type.builtin_type) {
				case Variant::ARRAY:
					return !p_type.has_container_element_type(0) || _signature_type_is_encodable(p_type.get_container_element_type(0));
				case Variant::DICTIONARY:
					return !p_type.has_container_element_types() ||
							(_signature_type_is_encodable(p_type.get_container_element_type_or_variant(0)) &&
									_signature_type_is_encodable(p_type.get_container_element_type_or_variant(1)));
				case Variant::CALLABLE:
				case Variant::SIGNAL: {
					if (!p_type.has_explicit_method_signature) {
						return true;
					}
					// The hint encodes only a fixed parameter/return list. Default-argument and vararg
					// arity metadata cannot round-trip, so a callable carrying it must cross untyped to
					// avoid rejecting valid default-arg/vararg calls at the script-API boundary.
					if (!p_type.method_info.default_arguments.is_empty() ||
							(p_type.method_info.flags & METHOD_FLAG_VARARG) != 0) {
						return false;
					}
					for (const GDScriptParser::DataType &parameter_type : p_type.method_parameter_types) {
						if (!_signature_type_is_encodable(parameter_type)) {
							return false;
						}
					}
					if (p_type.builtin_type == Variant::CALLABLE) {
						for (const GDScriptParser::DataType &return_type : p_type.method_return_type) {
							if (!_signature_type_is_encodable(return_type)) {
								return false;
							}
						}
					}
					return true;
				}
				default:
					return true;
			}
		case GDScriptParser::DataType::NATIVE:
			return ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type);
		case GDScriptParser::DataType::SCRIPT:
		case GDScriptParser::DataType::CLASS:
			// A user script/class leaf is encoded by name but always decoded back as a SCRIPT kind, while
			// a local annotation of the same class may resolve to a CLASS handle. Strict signature
			// equality compares kinds, so an encoded user-class slot could be falsely rejected after the
			// boundary. Treat these as non-round-trippable until the decoder/comparison agree on a kind;
			// such callables cross untyped (gradual). Native classes are unaffected and still round-trip.
			return false;
		case GDScriptParser::DataType::ENUM:
			// Global, native-class, and built-in enums encode an identity the decoder rebuilds exactly; a
			// script/class enum has no such name in the flat grammar (mirroring the non-global script/class
			// leaf limitation) and must cross untyped to avoid a false mismatch.
			return _enum_signature_leaf_round_trips(_encode_signature_leaf_name(p_type));
		case GDScriptParser::DataType::TYPE_PARAMETER:
		case GDScriptParser::DataType::RESOLVING:
		case GDScriptParser::DataType::UNRESOLVED:
			return false;
	}
	return false;
}

// Encodes a Coroutine[T] container element (a typed-array element or a typed-dictionary key/value) into
// the flat element-hint grammar, mirroring the top-level PROPERTY_HINT_COROUTINE_TYPE encoding. A
// faithfully round-trippable result yields "Coroutine[<result>]"; an absent or non-round-trippable
// result degrades to a result-less "Coroutine" rather than masquerading as a concrete "Coroutine[Variant]"
// (matching DataType::to_property_info's top-level lossy handling). The runtime typed-container element
// stays GDScriptFunctionState via the in-memory bytecode descriptor and is unaffected by this hint string;
// only the cross-script analyzer round-trip reads it (see GDScriptAnalyzer::type_from_property).
static String _encode_coroutine_container_element(const GDScriptParser::DataType &p_coroutine) {
	if (p_coroutine.has_container_element_type(0) && _signature_type_is_encodable(p_coroutine.get_container_element_type(0))) {
		return vformat("Coroutine[%s]", _encode_coroutine_result_element(p_coroutine));
	}
	// The bracketed empty slot keeps a result-less coroutine element unambiguous: a bare "Coroutine"
	// element name would collide with an ordinary class named Coroutine (only `Coroutine[...]` is the
	// reserved coroutine syntax), so a class used as Array[Coroutine] must stay decodable as that class.
	return "Coroutine[]";
}

PropertyInfo GDScriptParser::DataType::to_property_info(const String &p_name) const {
	PropertyInfo result;
	result.name = p_name;
	result.usage = PROPERTY_USAGE_NONE;

	if (!is_hard_type()) {
		result.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		return result;
	}

	switch (kind) {
		case BUILTIN:
			result.type = builtin_type;
			if ((builtin_type == Variant::CALLABLE || builtin_type == Variant::SIGNAL) && has_explicit_method_signature && _signature_type_is_encodable(*this)) {
				result.hint = PROPERTY_HINT_CALLABLE_TYPE;
				result.hint_string = _encode_method_signature_suffix(*this, builtin_type == Variant::CALLABLE);
				// The hint string carries only the signature suffix; the decoder rebuilds the leading
				// type name from the property type, which cannot express AsyncCallable. Tag an async
				// callable with a leading marker so the async-ness survives this boundary (signals are
				// never async). The marker is stripped on decode (see type_from_property).
				if (builtin_type == Variant::CALLABLE && signature_is_async) {
					result.hint_string = "async " + result.hint_string;
				}
			} else if (builtin_type == Variant::CALLABLE && signature_is_async) {
				// A bare AsyncCallable (async marker without an explicit signature) gets no signature
				// suffix, but still needs the marker so its async-ness survives the PropertyInfo boundary
				// and it stays distinct from a plain Callable. The marker alone is decoded back into
				// signature_is_async (see type_from_property).
				result.hint = PROPERTY_HINT_CALLABLE_TYPE;
				result.hint_string = "async";
			} else if (builtin_type == Variant::ARRAY && has_container_element_type(0) && get_container_element_type(0).is_coroutine) {
				// A Coroutine[T] array element keeps its coroutine identity and phantom result type T across
				// the cross-script boundary via the signature-grammar element hint (decoded in
				// type_from_property), instead of leaking the bare GDScriptFunctionState native name and
				// erasing T to Array[GDScriptFunctionState].
				result.hint = PROPERTY_HINT_ARRAY_TYPE;
				result.hint_string = _encode_coroutine_container_element(get_container_element_type(0));
			} else if (builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				const DataType elem_type = get_container_element_type(0);
				switch (elem_type.kind) {
					case BUILTIN:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = Variant::get_type_name(elem_type.builtin_type);
						break;
					case NATIVE:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = elem_type.native_type;
						break;
					case SCRIPT:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						if (elem_type.script_type.is_valid() && elem_type.script_type->get_global_name() != StringName()) {
							result.hint_string = elem_type.script_type->get_global_name();
						} else {
							result.hint_string = elem_type.native_type;
						}
						break;
					case CLASS:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						if (elem_type.class_type != nullptr && elem_type.class_type->get_global_name() != StringName()) {
							result.hint_string = elem_type.class_type->get_global_name();
						} else {
							result.hint_string = elem_type.native_type;
						}
						break;
					case ENUM:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = String(elem_type.native_type).replace("::", ".");
						break;
					case TYPE_PARAMETER:
					case VARIANT:
					case RESOLVING:
					case UNRESOLVED:
						break;
				}
			} else if (builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				const DataType key_type = get_container_element_type_or_variant(0);
				const DataType value_type = get_container_element_type_or_variant(1);
				if ((key_type.kind == VARIANT && value_type.kind == VARIANT) || key_type.kind == RESOLVING ||
						key_type.kind == UNRESOLVED || value_type.kind == RESOLVING || value_type.kind == UNRESOLVED) {
					break;
				}
				String key_hint, value_hint;
				switch (key_type.kind) {
					case BUILTIN:
						key_hint = Variant::get_type_name(key_type.builtin_type);
						break;
					case NATIVE:
						key_hint = key_type.native_type;
						break;
					case SCRIPT:
						if (key_type.script_type.is_valid() && key_type.script_type->get_global_name() != StringName()) {
							key_hint = key_type.script_type->get_global_name();
						} else {
							key_hint = key_type.native_type;
						}
						break;
					case CLASS:
						if (key_type.class_type != nullptr && key_type.class_type->get_global_name() != StringName()) {
							key_hint = key_type.class_type->get_global_name();
						} else {
							key_hint = key_type.native_type;
						}
						break;
					case ENUM:
						key_hint = String(key_type.native_type).replace("::", ".");
						break;
					default:
						key_hint = "Variant";
						break;
				}
				switch (value_type.kind) {
					case BUILTIN:
						value_hint = Variant::get_type_name(value_type.builtin_type);
						break;
					case NATIVE:
						value_hint = value_type.native_type;
						break;
					case SCRIPT:
						if (value_type.script_type.is_valid() && value_type.script_type->get_global_name() != StringName()) {
							value_hint = value_type.script_type->get_global_name();
						} else {
							value_hint = value_type.native_type;
						}
						break;
					case CLASS:
						if (value_type.class_type != nullptr && value_type.class_type->get_global_name() != StringName()) {
							value_hint = value_type.class_type->get_global_name();
						} else {
							value_hint = value_type.native_type;
						}
						break;
					case ENUM:
						value_hint = String(value_type.native_type).replace("::", ".");
						break;
					default:
						value_hint = "Variant";
						break;
				}
				// A Coroutine[T] key/value element round-trips its coroutine identity and phantom result type
				// T via the signature-grammar element hint (decoded in type_from_property), overriding the
				// bare GDScriptFunctionState native name the NATIVE switch case emits above. See the array
				// branch and _encode_coroutine_container_element.
				if (key_type.is_coroutine) {
					key_hint = _encode_coroutine_container_element(key_type);
				}
				if (value_type.is_coroutine) {
					value_hint = _encode_coroutine_container_element(value_type);
				}
				result.hint = PROPERTY_HINT_DICTIONARY_TYPE;
				result.hint_string = key_hint + ";" + value_hint;
			}
			break;
		case NATIVE:
			result.type = Variant::OBJECT;
			if (is_meta_type) {
				result.class_name = GDScriptNativeClass::get_class_static();
			} else if (is_coroutine) {
				// Coroutine[T] is a source-level skin over GDScriptFunctionState. Preserve both the
				// coroutine identity and the phantom result type T across the PropertyInfo boundary via a
				// dedicated hint (mirroring PROPERTY_HINT_ARRAY_TYPE), decoded in type_from_property.
				// Without this the skin leaks as a bare GDScriptFunctionState and a cross-script consumer
				// loses T and awaitability.
				result.class_name = native_type;
				result.hint = PROPERTY_HINT_COROUTINE_TYPE;
				if (!has_container_element_type(0) || !_signature_type_is_encodable(get_container_element_type(0))) {
					// A result type that cannot round-trip faithfully (e.g. a script class) is dropped: the
					// handle crosses as a result-less coroutine rather than masquerading as a concrete
					// Coroutine[Variant]. This stays gradually compatible at assignment while a signature
					// slot carrying it is treated as comparison-unsafe (see _signature_slot_is_comparison_safe)
					// so it crosses gradually instead of becoming a false strict mismatch.
					result.hint_string = "";
				} else {
					result.hint_string = _encode_coroutine_result_element(*this);
				}
			} else {
				result.class_name = native_type;
			}
			break;
		case SCRIPT:
			result.type = Variant::OBJECT;
			if (is_meta_type) {
				result.class_name = script_type.is_valid() ? script_type->get_class_name() : Script::get_class_static();
			} else if (script_type.is_valid() && script_type->get_global_name() != StringName()) {
				result.class_name = script_type->get_global_name();
			} else {
				result.class_name = native_type;
			}
			break;
		case CLASS:
			result.type = Variant::OBJECT;
			if (is_meta_type) {
				result.class_name = GDScript::get_class_static();
			} else if (class_type != nullptr && class_type->get_global_name() != StringName()) {
				result.class_name = class_type->get_global_name();
			} else {
				result.class_name = native_type;
			}
			break;
		case ENUM:
			if (is_meta_type) {
				result.type = Variant::DICTIONARY;
			} else {
				result.type = Variant::INT;
				result.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
				result.class_name = String(native_type).replace("::", ".");
			}
			break;
		case TYPE_PARAMETER:
			// Type parameters are erased to Variant outside the type checker.
		case VARIANT:
		case RESOLVING:
		case UNRESOLVED:
			result.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
			break;
	}

	if (is_nullable && result.type == Variant::OBJECT && result.class_name != StringName()) {
		result.class_name = String(result.class_name) + "?";
	}

	return result;
}

static Variant::Type _variant_type_to_typed_array_element_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
			return Variant::INT;
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
			return Variant::FLOAT;
		case Variant::PACKED_STRING_ARRAY:
			return Variant::STRING;
		case Variant::PACKED_VECTOR2_ARRAY:
			return Variant::VECTOR2;
		case Variant::PACKED_VECTOR3_ARRAY:
			return Variant::VECTOR3;
		case Variant::PACKED_COLOR_ARRAY:
			return Variant::COLOR;
		case Variant::PACKED_VECTOR4_ARRAY:
			return Variant::VECTOR4;
		default:
			return Variant::NIL;
	}
}

bool GDScriptParser::DataType::is_typed_container_type() const {
	return kind == GDScriptParser::DataType::BUILTIN && _variant_type_to_typed_array_element_type(builtin_type) != Variant::NIL;
}

GDScriptParser::DataType GDScriptParser::DataType::get_typed_container_type() const {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.builtin_type = _variant_type_to_typed_array_element_type(builtin_type);
	return type;
}

bool GDScriptParser::DataType::can_reference(const GDScriptParser::DataType &p_other) const {
	if (p_other.is_meta_type) {
		return false;
	} else if (builtin_type != p_other.builtin_type) {
		return false;
	} else if (builtin_type != Variant::OBJECT) {
		return true;
	}

	if (native_type == StringName()) {
		return true;
	} else if (p_other.native_type == StringName()) {
		return false;
	} else if (native_type != p_other.native_type && !ClassDB::is_parent_class(p_other.native_type, native_type)) {
		return false;
	}

	Ref<Script> script = script_type;
	if (kind == GDScriptParser::DataType::CLASS && script.is_null()) {
		Error err = OK;
		Ref<GDScript> scr = GDScriptCache::get_shallow_script(script_path, err);
		ERR_FAIL_COND_V_MSG(err, false, vformat(R"(Error while getting cache for script "%s".)", script_path));
		script.reference_ptr(scr->find_class(class_type->fqcn));
	}

	Ref<Script> script_other = p_other.script_type;
	if (p_other.kind == GDScriptParser::DataType::CLASS && script_other.is_null()) {
		Error err = OK;
		Ref<GDScript> scr = GDScriptCache::get_shallow_script(p_other.script_path, err);
		ERR_FAIL_COND_V_MSG(err, false, vformat(R"(Error while getting cache for script "%s".)", p_other.script_path));
		script_other.reference_ptr(scr->find_class(p_other.class_type->fqcn));
	}

	if (script.is_null()) {
		return true;
	} else if (script_other.is_null()) {
		return false;
	} else if (script != script_other && !script_other->inherits_script(script)) {
		return false;
	}

	return true;
}

void GDScriptParser::complete_extents(Node *p_node) {
	while (!nodes_in_progress.is_empty() && nodes_in_progress.back()->get() != p_node) {
		ERR_PRINT("Parser bug: Mismatch in extents tracking stack.");
		nodes_in_progress.pop_back();
	}
	if (nodes_in_progress.is_empty()) {
		ERR_PRINT("Parser bug: Extents tracking stack is empty.");
	} else {
		nodes_in_progress.pop_back();
	}
}

void GDScriptParser::update_extents(Node *p_node) {
	p_node->end_line = previous.end_line;
	p_node->end_column = previous.end_column;
}

void GDScriptParser::reset_extents(Node *p_node, GDScriptTokenizer::Token p_token) {
	p_node->start_line = p_token.start_line;
	p_node->end_line = p_token.end_line;
	p_node->start_column = p_token.start_column;
	p_node->end_column = p_token.end_column;
}

void GDScriptParser::reset_extents(Node *p_node, Node *p_from) {
	if (p_from == nullptr) {
		return;
	}
	p_node->start_line = p_from->start_line;
	p_node->end_line = p_from->end_line;
	p_node->start_column = p_from->start_column;
	p_node->end_column = p_from->end_column;
}

/*---------- PRETTY PRINT FOR DEBUG ----------*/

#ifdef DEBUG_ENABLED

void GDScriptParser::TreePrinter::increase_indent() {
	indent_level++;
	indent = "";
	for (int i = 0; i < indent_level * 4; i++) {
		if (i % 4 == 0) {
			indent += "|";
		} else {
			indent += " ";
		}
	}
}

void GDScriptParser::TreePrinter::decrease_indent() {
	indent_level--;
	indent = "";
	for (int i = 0; i < indent_level * 4; i++) {
		if (i % 4 == 0) {
			indent += "|";
		} else {
			indent += " ";
		}
	}
}

void GDScriptParser::TreePrinter::push_line(const String &p_line) {
	if (!p_line.is_empty()) {
		push_text(p_line);
	}
	printed += "\n";
	pending_indent = true;
}

void GDScriptParser::TreePrinter::push_text(const String &p_text) {
	if (pending_indent) {
		printed += indent;
		pending_indent = false;
	}
	printed += p_text;
}

void GDScriptParser::TreePrinter::print_annotation(const AnnotationNode *p_annotation) {
	push_text(p_annotation->name);
	push_text(" (");
	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_annotation->arguments[i]);
	}
	push_line(")");
}

void GDScriptParser::TreePrinter::print_annotation_declaration(AnnotationDeclarationNode *p_annotation_declaration) {
	push_text("Annotation ");
	if (p_annotation_declaration->identifier == nullptr) {
		push_text("<unnamed>");
	} else {
		print_identifier(p_annotation_declaration->identifier);
	}

	push_text("(");
	for (int i = 0; i < p_annotation_declaration->parameters.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		print_parameter(p_annotation_declaration->parameters[i]);
	}
	if (p_annotation_declaration->rest_parameter != nullptr) {
		if (!p_annotation_declaration->parameters.is_empty()) {
			push_text(", ");
		}
		push_text("...");
		print_parameter(p_annotation_declaration->rest_parameter);
	}
	push_text(")");

	push_text(" targets ");
	bool first = true;
	const uint32_t targets = p_annotation_declaration->targets;
	if (targets & AnnotationDeclarationNode::TARGET_CLASS) {
		push_text("CLASS");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_METHOD) {
		push_text(first ? "METHOD" : ", METHOD");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_VARIABLE) {
		push_text(first ? "VARIABLE" : ", VARIABLE");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_SIGNAL) {
		push_text(first ? "SIGNAL" : ", SIGNAL");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_CONSTANT) {
		push_text(first ? "CONSTANT" : ", CONSTANT");
		first = false;
	}

	push_line(vformat(" [%s]", p_annotation_declaration->qualified_name));
}

void GDScriptParser::TreePrinter::print_array(ArrayNode *p_array) {
	push_text("[ ");
	for (int i = 0; i < p_array->elements.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_array->elements[i]);
	}
	push_text(" ]");
}

void GDScriptParser::TreePrinter::print_assert(AssertNode *p_assert) {
	push_text("Assert ( ");
	print_expression(p_assert->condition);
	push_line(" )");
}

void GDScriptParser::TreePrinter::print_assignment(AssignmentNode *p_assignment) {
	switch (p_assignment->assignee->type) {
		case Node::IDENTIFIER:
			print_identifier(static_cast<IdentifierNode *>(p_assignment->assignee));
			break;
		case Node::SUBSCRIPT:
			print_subscript(static_cast<SubscriptNode *>(p_assignment->assignee));
			break;
		default:
			break; // Unreachable.
	}

	push_text(" ");
	switch (p_assignment->operation) {
		case AssignmentNode::OP_ADDITION:
			push_text("+");
			break;
		case AssignmentNode::OP_SUBTRACTION:
			push_text("-");
			break;
		case AssignmentNode::OP_MULTIPLICATION:
			push_text("*");
			break;
		case AssignmentNode::OP_DIVISION:
			push_text("/");
			break;
		case AssignmentNode::OP_MODULO:
			push_text("%");
			break;
		case AssignmentNode::OP_POWER:
			push_text("**");
			break;
		case AssignmentNode::OP_BIT_SHIFT_LEFT:
			push_text("<<");
			break;
		case AssignmentNode::OP_BIT_SHIFT_RIGHT:
			push_text(">>");
			break;
		case AssignmentNode::OP_BIT_AND:
			push_text("&");
			break;
		case AssignmentNode::OP_BIT_OR:
			push_text("|");
			break;
		case AssignmentNode::OP_BIT_XOR:
			push_text("^");
			break;
		case AssignmentNode::OP_NONE:
			break;
	}
	push_text("= ");
	print_expression(p_assignment->assigned_value);
	push_line();
}

void GDScriptParser::TreePrinter::print_await(AwaitNode *p_await) {
	push_text("Await ");
	print_expression(p_await->to_await);
}

void GDScriptParser::TreePrinter::print_binary_op(BinaryOpNode *p_binary_op) {
	// Surround in parenthesis for disambiguation.
	push_text("(");
	print_expression(p_binary_op->left_operand);
	switch (p_binary_op->operation) {
		case BinaryOpNode::OP_ADDITION:
			push_text(" + ");
			break;
		case BinaryOpNode::OP_SUBTRACTION:
			push_text(" - ");
			break;
		case BinaryOpNode::OP_MULTIPLICATION:
			push_text(" * ");
			break;
		case BinaryOpNode::OP_DIVISION:
			push_text(" / ");
			break;
		case BinaryOpNode::OP_MODULO:
			push_text(" % ");
			break;
		case BinaryOpNode::OP_POWER:
			push_text(" ** ");
			break;
		case BinaryOpNode::OP_BIT_LEFT_SHIFT:
			push_text(" << ");
			break;
		case BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			push_text(" >> ");
			break;
		case BinaryOpNode::OP_BIT_AND:
			push_text(" & ");
			break;
		case BinaryOpNode::OP_BIT_OR:
			push_text(" | ");
			break;
		case BinaryOpNode::OP_BIT_XOR:
			push_text(" ^ ");
			break;
		case BinaryOpNode::OP_LOGIC_AND:
			push_text(" AND ");
			break;
		case BinaryOpNode::OP_LOGIC_OR:
			push_text(" OR ");
			break;
		case BinaryOpNode::OP_CONTENT_TEST:
			push_text(" IN ");
			break;
		case BinaryOpNode::OP_COMP_EQUAL:
			push_text(" == ");
			break;
		case BinaryOpNode::OP_COMP_NOT_EQUAL:
			push_text(" != ");
			break;
		case BinaryOpNode::OP_COMP_LESS:
			push_text(" < ");
			break;
		case BinaryOpNode::OP_COMP_LESS_EQUAL:
			push_text(" <= ");
			break;
		case BinaryOpNode::OP_COMP_GREATER:
			push_text(" > ");
			break;
		case BinaryOpNode::OP_COMP_GREATER_EQUAL:
			push_text(" >= ");
			break;
	}
	print_expression(p_binary_op->right_operand);
	// Surround in parenthesis for disambiguation.
	push_text(")");
}

void GDScriptParser::TreePrinter::print_call(CallNode *p_call) {
	if (p_call->is_super) {
		push_text("super");
		if (p_call->callee != nullptr) {
			push_text(".");
			print_expression(p_call->callee);
		}
	} else {
		print_expression(p_call->callee);
	}
	push_text("( ");
	for (int i = 0; i < p_call->arguments.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_call->arguments[i]);
	}
	push_text(" )");
}

void GDScriptParser::TreePrinter::print_cast(CastNode *p_cast) {
	print_expression(p_cast->operand);
	push_text(" AS ");
	print_type(p_cast->cast_type);
}

void GDScriptParser::TreePrinter::print_class(ClassNode *p_class) {
	for (const AnnotationNode *E : p_class->annotations) {
		print_annotation(E);
	}
	push_text(p_class->is_trait ? "Trait " : "Class ");
	if (p_class->identifier == nullptr) {
		push_text("<unnamed>");
	} else {
		print_identifier(p_class->identifier);
	}

	print_type_parameters(p_class->type_parameters);

	if (p_class->extends_used) {
		bool first = true;
		push_text(" Extends ");
		if (!p_class->extends_path.is_empty()) {
			push_text(vformat(R"("%s")", p_class->extends_path));
			first = false;
		}
		for (int i = 0; i < p_class->extends.size(); i++) {
			if (!first) {
				push_text(".");
			} else {
				first = false;
			}
			push_text(p_class->extends[i]->name);
		}
	}

	if (!p_class->used_traits.is_empty()) {
		push_text(" Uses ");
		for (int i = 0; i < p_class->used_traits.size(); i++) {
			if (i > 0) {
				push_text(", ");
			}
			push_text(p_class->used_traits[i].to_string());
		}
	}

	push_line(" :");

	increase_indent();

	for (AnnotationDeclarationNode *E : p_class->annotation_declarations) {
		print_annotation_declaration(E);
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const ClassNode::Member &m = p_class->members[i];

		switch (m.type) {
			case ClassNode::Member::CLASS:
				print_class(m.m_class);
				break;
			case ClassNode::Member::VARIABLE:
				print_variable(m.variable);
				break;
			case ClassNode::Member::CONSTANT:
				print_constant(m.constant);
				break;
			case ClassNode::Member::SIGNAL:
				print_signal(m.signal);
				break;
			case ClassNode::Member::FUNCTION:
				print_function(m.function);
				break;
			case ClassNode::Member::ENUM:
				print_enum(m.m_enum);
				break;
			case ClassNode::Member::ENUM_VALUE:
				break; // Nothing. Will be printed by enum.
			case ClassNode::Member::GROUP:
				break; // Nothing. Groups are only used by inspector.
			case ClassNode::Member::UNDEFINED:
				push_line("<unknown member>");
				break;
		}
	}

	decrease_indent();
}

void GDScriptParser::TreePrinter::print_constant(ConstantNode *p_constant) {
	push_text("Constant ");
	print_identifier(p_constant->identifier);

	increase_indent();

	push_line();
	push_text("= ");
	if (p_constant->initializer == nullptr) {
		push_text("<missing value>");
	} else {
		print_expression(p_constant->initializer);
	}
	decrease_indent();
	push_line();
}

void GDScriptParser::TreePrinter::print_dictionary(DictionaryNode *p_dictionary) {
	push_line("{");
	increase_indent();
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		print_expression(p_dictionary->elements[i].key);
		if (p_dictionary->style == DictionaryNode::PYTHON_DICT) {
			push_text(" : ");
		} else {
			push_text(" = ");
		}
		print_expression(p_dictionary->elements[i].value);
		push_line(" ,");
	}
	decrease_indent();
	push_text("}");
}

void GDScriptParser::TreePrinter::print_expression(ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		push_text("<invalid expression>");
		return;
	}
	switch (p_expression->type) {
		case Node::ARRAY:
			print_array(static_cast<ArrayNode *>(p_expression));
			break;
		case Node::ASSIGNMENT:
			print_assignment(static_cast<AssignmentNode *>(p_expression));
			break;
		case Node::AWAIT:
			print_await(static_cast<AwaitNode *>(p_expression));
			break;
		case Node::BINARY_OPERATOR:
			print_binary_op(static_cast<BinaryOpNode *>(p_expression));
			break;
		case Node::CALL:
			print_call(static_cast<CallNode *>(p_expression));
			break;
		case Node::CAST:
			print_cast(static_cast<CastNode *>(p_expression));
			break;
		case Node::DICTIONARY:
			print_dictionary(static_cast<DictionaryNode *>(p_expression));
			break;
		case Node::GET_NODE:
			print_get_node(static_cast<GetNodeNode *>(p_expression));
			break;
		case Node::IDENTIFIER:
			print_identifier(static_cast<IdentifierNode *>(p_expression));
			break;
		case Node::LAMBDA:
			print_lambda(static_cast<LambdaNode *>(p_expression));
			break;
		case Node::LITERAL:
			print_literal(static_cast<LiteralNode *>(p_expression));
			break;
		case Node::PRELOAD:
			print_preload(static_cast<PreloadNode *>(p_expression));
			break;
		case Node::SELF:
			print_self(static_cast<SelfNode *>(p_expression));
			break;
		case Node::SUBSCRIPT:
			print_subscript(static_cast<SubscriptNode *>(p_expression));
			break;
		case Node::TERNARY_OPERATOR:
			print_ternary_op(static_cast<TernaryOpNode *>(p_expression));
			break;
		case Node::TYPE_TEST:
			print_type_test(static_cast<TypeTestNode *>(p_expression));
			break;
		case Node::UNARY_OPERATOR:
			print_unary_op(static_cast<UnaryOpNode *>(p_expression));
			break;
		default:
			push_text(vformat("<unknown expression %d>", p_expression->type));
			break;
	}
}

void GDScriptParser::TreePrinter::print_enum(EnumNode *p_enum) {
	push_text("Enum ");
	if (p_enum->identifier != nullptr) {
		print_identifier(p_enum->identifier);
	} else {
		push_text("<unnamed>");
	}

	push_line(" {");
	increase_indent();
	for (int i = 0; i < p_enum->values.size(); i++) {
		const EnumNode::Value &item = p_enum->values[i];
		print_identifier(item.identifier);
		push_text(" = ");
		push_text(itos(item.value));
		push_line(" ,");
	}
	decrease_indent();
	push_line("}");
}

void GDScriptParser::TreePrinter::print_for(ForNode *p_for) {
	push_text("For ");
	print_identifier(p_for->variable);
	push_text(" IN ");
	print_expression(p_for->list);
	push_line(" :");

	increase_indent();

	print_suite(p_for->loop);

	decrease_indent();
}

void GDScriptParser::TreePrinter::print_function(FunctionNode *p_function, const String &p_context) {
	for (const AnnotationNode *E : p_function->annotations) {
		print_annotation(E);
	}
	if (p_function->is_static) {
		push_text("Static ");
	}
	if (p_function->is_declared_async) {
		push_text("Async ");
	}
	push_text(p_context);
	push_text(" ");
	if (p_function->identifier) {
		print_identifier(p_function->identifier);
	} else {
		push_text("<anonymous>");
	}
	print_type_parameters(p_function->type_parameters);
	push_text("( ");
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_parameter(p_function->parameters[i]);
	}
	push_line(" ) :");
	increase_indent();
	print_suite(p_function->body);
	decrease_indent();
}

void GDScriptParser::TreePrinter::print_get_node(GetNodeNode *p_get_node) {
	if (p_get_node->use_dollar) {
		push_text("$");
	}
	push_text(p_get_node->full_path);
}

void GDScriptParser::TreePrinter::print_identifier(IdentifierNode *p_identifier) {
	if (p_identifier != nullptr) {
		push_text(p_identifier->name);
	} else {
		push_text("<invalid identifier>");
	}
}

void GDScriptParser::TreePrinter::print_if(IfNode *p_if, bool p_is_elif) {
	if (p_is_elif) {
		push_text("Elif ");
	} else {
		push_text("If ");
	}
	print_expression(p_if->condition);
	push_line(" :");

	increase_indent();
	print_suite(p_if->true_block);
	decrease_indent();

	// FIXME: Properly detect "elif" blocks.
	if (p_if->false_block != nullptr) {
		push_line("Else :");
		increase_indent();
		print_suite(p_if->false_block);
		decrease_indent();
	}
}

void GDScriptParser::TreePrinter::print_lambda(LambdaNode *p_lambda) {
	print_function(p_lambda->function, "Lambda");
	push_text("| captures [ ");
	for (int i = 0; i < p_lambda->captures.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		push_text(p_lambda->captures[i]->name.operator String());
	}
	push_line(" ]");
}

void GDScriptParser::TreePrinter::print_literal(LiteralNode *p_literal) {
	// Prefix for string types.
	switch (p_literal->value.get_type()) {
		case Variant::NODE_PATH:
			push_text("^\"");
			break;
		case Variant::STRING:
			push_text("\"");
			break;
		case Variant::STRING_NAME:
			push_text("&\"");
			break;
		default:
			break;
	}
	push_text(p_literal->value);
	// Suffix for string types.
	switch (p_literal->value.get_type()) {
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::STRING_NAME:
			push_text("\"");
			break;
		default:
			break;
	}
}

void GDScriptParser::TreePrinter::print_match(MatchNode *p_match) {
	push_text("Match ");
	print_expression(p_match->test);
	push_line(" :");

	increase_indent();
	for (int i = 0; i < p_match->branches.size(); i++) {
		print_match_branch(p_match->branches[i]);
	}
	decrease_indent();
}

void GDScriptParser::TreePrinter::print_match_branch(MatchBranchNode *p_match_branch) {
	for (int i = 0; i < p_match_branch->patterns.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_match_pattern(p_match_branch->patterns[i]);
	}

	push_line(" :");

	increase_indent();
	print_suite(p_match_branch->block);
	decrease_indent();
}

void GDScriptParser::TreePrinter::print_match_pattern(PatternNode *p_match_pattern) {
	switch (p_match_pattern->pattern_type) {
		case PatternNode::PT_LITERAL:
			print_literal(p_match_pattern->literal);
			break;
		case PatternNode::PT_WILDCARD:
			push_text("_");
			break;
		case PatternNode::PT_REST:
			push_text("..");
			break;
		case PatternNode::PT_BIND:
			push_text("Var ");
			print_identifier(p_match_pattern->bind);
			break;
		case PatternNode::PT_EXPRESSION:
			print_expression(p_match_pattern->expression);
			break;
		case PatternNode::PT_ARRAY:
			push_text("[ ");
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				print_match_pattern(p_match_pattern->array[i]);
			}
			push_text(" ]");
			break;
		case PatternNode::PT_DICTIONARY:
			push_text("{ ");
			for (int i = 0; i < p_match_pattern->dictionary.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				if (p_match_pattern->dictionary[i].key != nullptr) {
					// Key can be null for rest pattern.
					print_expression(p_match_pattern->dictionary[i].key);
					push_text(" : ");
				}
				print_match_pattern(p_match_pattern->dictionary[i].value_pattern);
			}
			push_text(" }");
			break;
	}
}

void GDScriptParser::TreePrinter::print_parameter(ParameterNode *p_parameter) {
	print_identifier(p_parameter->identifier);
	if (p_parameter->datatype_specifier != nullptr) {
		push_text(" : ");
		print_type(p_parameter->datatype_specifier);
	}
	if (p_parameter->initializer != nullptr) {
		push_text(" = ");
		print_expression(p_parameter->initializer);
	}
}

void GDScriptParser::TreePrinter::print_preload(PreloadNode *p_preload) {
	push_text(R"(Preload ( ")");
	push_text(p_preload->resolved_path);
	push_text(R"(" )");
}

void GDScriptParser::TreePrinter::print_return(ReturnNode *p_return) {
	push_text("Return");
	if (p_return->return_value != nullptr) {
		push_text(" ");
		print_expression(p_return->return_value);
	}
	push_line();
}

void GDScriptParser::TreePrinter::print_self(SelfNode *p_self) {
	push_text("Self(");
	if (p_self->current_class->identifier != nullptr) {
		print_identifier(p_self->current_class->identifier);
	} else {
		push_text("<main class>");
	}
	push_text(")");
}

void GDScriptParser::TreePrinter::print_signal(SignalNode *p_signal) {
	push_text("Signal ");
	print_identifier(p_signal->identifier);
	push_text("( ");
	for (int i = 0; i < p_signal->parameters.size(); i++) {
		print_parameter(p_signal->parameters[i]);
	}
	push_line(" )");
}

void GDScriptParser::TreePrinter::print_subscript(SubscriptNode *p_subscript) {
	print_expression(p_subscript->base);
	if (p_subscript->is_attribute) {
		push_text(".");
		print_identifier(p_subscript->attribute);
	} else {
		push_text("[ ");
		print_expression(p_subscript->index);
		push_text(" ]");
	}
}

void GDScriptParser::TreePrinter::print_statement(Node *p_statement) {
	switch (p_statement->type) {
		case Node::ASSERT:
			print_assert(static_cast<AssertNode *>(p_statement));
			break;
		case Node::VARIABLE:
			print_variable(static_cast<VariableNode *>(p_statement));
			break;
		case Node::CONSTANT:
			print_constant(static_cast<ConstantNode *>(p_statement));
			break;
		case Node::IF:
			print_if(static_cast<IfNode *>(p_statement));
			break;
		case Node::FOR:
			print_for(static_cast<ForNode *>(p_statement));
			break;
		case Node::WHILE:
			print_while(static_cast<WhileNode *>(p_statement));
			break;
		case Node::MATCH:
			print_match(static_cast<MatchNode *>(p_statement));
			break;
		case Node::RETURN:
			print_return(static_cast<ReturnNode *>(p_statement));
			break;
		case Node::BREAK:
			push_line("Break");
			break;
		case Node::CONTINUE:
			push_line("Continue");
			break;
		case Node::PASS:
			push_line("Pass");
			break;
		case Node::BREAKPOINT:
			push_line("Breakpoint");
			break;
		case Node::ASSIGNMENT:
			print_assignment(static_cast<AssignmentNode *>(p_statement));
			break;
		default:
			if (p_statement->is_expression()) {
				print_expression(static_cast<ExpressionNode *>(p_statement));
				push_line();
			} else {
				push_line(vformat("<unknown statement %d>", p_statement->type));
			}
			break;
	}
}

void GDScriptParser::TreePrinter::print_suite(SuiteNode *p_suite) {
	for (int i = 0; i < p_suite->statements.size(); i++) {
		print_statement(p_suite->statements[i]);
	}
}

void GDScriptParser::TreePrinter::print_ternary_op(TernaryOpNode *p_ternary_op) {
	// Surround in parenthesis for disambiguation.
	push_text("(");
	print_expression(p_ternary_op->true_expr);
	push_text(") IF (");
	print_expression(p_ternary_op->condition);
	push_text(") ELSE (");
	print_expression(p_ternary_op->false_expr);
	push_text(")");
}

void GDScriptParser::TreePrinter::print_type(TypeNode *p_type) {
	if (p_type->type_chain.is_empty()) {
		push_text("Void");
	} else {
		for (int i = 0; i < p_type->type_chain.size(); i++) {
			if (i > 0) {
				push_text(".");
			}
			print_identifier(p_type->type_chain[i]);
		}
	}
	if (p_type->is_nullable) {
		push_text("?");
	}
}

void GDScriptParser::TreePrinter::print_type_parameters(const Vector<TypeParameterNode *> &p_type_parameters) {
	if (p_type_parameters.is_empty()) {
		return;
	}
	push_text("[");
	for (int i = 0; i < p_type_parameters.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		const TypeParameterNode *type_parameter = p_type_parameters[i];
		if (type_parameter->identifier != nullptr) {
			push_text(type_parameter->identifier->name);
		} else {
			push_text("<unnamed>");
		}
		if (type_parameter->bound != nullptr) {
			push_text(": ");
			print_type(type_parameter->bound);
		}
	}
	push_text("]");
}

void GDScriptParser::TreePrinter::print_type_test(TypeTestNode *p_test) {
	print_expression(p_test->operand);
	push_text(" IS ");
	print_type(p_test->test_type);
}

void GDScriptParser::TreePrinter::print_unary_op(UnaryOpNode *p_unary_op) {
	// Surround in parenthesis for disambiguation.
	push_text("(");
	switch (p_unary_op->operation) {
		case UnaryOpNode::OP_POSITIVE:
			push_text("+");
			break;
		case UnaryOpNode::OP_NEGATIVE:
			push_text("-");
			break;
		case UnaryOpNode::OP_LOGIC_NOT:
			push_text("NOT");
			break;
		case UnaryOpNode::OP_COMPLEMENT:
			push_text("~");
			break;
	}
	print_expression(p_unary_op->operand);
	// Surround in parenthesis for disambiguation.
	push_text(")");
}

void GDScriptParser::TreePrinter::print_variable(VariableNode *p_variable) {
	for (const AnnotationNode *E : p_variable->annotations) {
		print_annotation(E);
	}

	if (p_variable->is_static) {
		push_text("Static ");
	}
	push_text("Variable ");
	print_identifier(p_variable->identifier);

	push_text(" : ");
	if (p_variable->datatype_specifier != nullptr) {
		print_type(p_variable->datatype_specifier);
	} else if (p_variable->infer_datatype) {
		push_text("<inferred type>");
	} else {
		push_text("Variant");
	}

	increase_indent();

	push_line();
	push_text("= ");
	if (p_variable->initializer == nullptr) {
		push_text("<default value>");
	} else {
		print_expression(p_variable->initializer);
	}
	push_line();

	if (p_variable->property != VariableNode::PROP_NONE) {
		if (p_variable->getter != nullptr) {
			push_text("Get");
			if (p_variable->property == VariableNode::PROP_INLINE) {
				push_line(":");
				increase_indent();
				print_suite(p_variable->getter->body);
				decrease_indent();
			} else {
				push_line(" =");
				increase_indent();
				print_identifier(p_variable->getter_pointer);
				push_line();
				decrease_indent();
			}
		}
		if (p_variable->setter != nullptr) {
			push_text("Set (");
			if (p_variable->property == VariableNode::PROP_INLINE) {
				if (p_variable->setter_parameter != nullptr) {
					print_identifier(p_variable->setter_parameter);
				} else {
					push_text("<missing>");
				}
				push_line("):");
				increase_indent();
				print_suite(p_variable->setter->body);
				decrease_indent();
			} else {
				push_line(" =");
				increase_indent();
				print_identifier(p_variable->setter_pointer);
				push_line();
				decrease_indent();
			}
		}
	}

	decrease_indent();
	push_line();
}

void GDScriptParser::TreePrinter::print_while(WhileNode *p_while) {
	push_text("While ");
	print_expression(p_while->condition);
	push_line(" :");

	increase_indent();
	print_suite(p_while->loop);
	decrease_indent();
}

void GDScriptParser::TreePrinter::print_tree(const GDScriptParser &p_parser) {
	ClassNode *class_tree = p_parser.get_tree();
	ERR_FAIL_NULL_MSG(class_tree, "Parse the code before printing the parse tree.");

	if (p_parser.is_tool()) {
		push_line("@tool");
	}
	if (!class_tree->icon_path.is_empty()) {
		push_text(R"(@icon (")");
		push_text(class_tree->icon_path);
		push_line("\")");
	}
	print_class(class_tree);

	print_line(String(printed));
}

#endif // DEBUG_ENABLED
