/**************************************************************************/
/*  gdscript_analyzer.cpp                                                 */
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

#include "gdscript_analyzer.h"

#include "gdscript.h"
#include "gdscript_type.h"
#include "gdscript_utility_callable.h"
#include "gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/core_constants.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "scene/main/node.h"

#if defined(TOOLS_ENABLED) && !defined(DISABLE_DEPRECATED)
#define SUGGEST_GODOT4_RENAMES
#include "editor/project_upgrade/renames_map_3_to_4.h"
#endif

#define UNNAMED_ENUM "<anonymous enum>"
#define ENUM_SEPARATOR "."

static GDScriptParser::DataType make_void_type() {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.builtin_type = Variant::NIL;
	return type;
}

static String identifier_name_from_expression(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression != nullptr && p_expression->type == GDScriptParser::Node::IDENTIFIER) {
		const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
		return identifier->name;
	}
	return String();
}

static String callable_type_string_with_signature(
		const GDScriptParser::DataType &p_callable_type,
		const Vector<GDScriptParser::DataType> &p_callable_parameter_types) {
	if (p_callable_type.has_explicit_method_signature || p_callable_parameter_types.is_empty()) {
		return p_callable_type.to_string();
	}

	GDScriptParser::DataType callable_type = p_callable_type;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_callable_parameter_types;
	callable_type.method_return_type.push_back(make_void_type());
	return callable_type.to_string();
}

static String _class_or_trait_name(const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	return p_class->fqcn.get_file();
}

static String _localize_script_path(const String &p_path) {
	if (ProjectSettings::get_singleton() == nullptr || p_path.is_empty()) {
		return p_path;
	}
	return ProjectSettings::get_singleton()->localize_path(p_path);
}

static String _trait_method_info_source(const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function) {
	if (p_class == nullptr) {
		return String();
	}

	String script_path = p_class->get_datatype().script_path;
	if (script_path.is_empty()) {
		script_path = p_class->fqcn;
	}
	if (script_path.is_empty()) {
		return String();
	}
	script_path = _localize_script_path(script_path);

	if (p_function != nullptr) {
		return vformat(R"(Implementation comes from "%s" line %d.)", script_path, p_function->start_line);
	}
	return vformat(R"(Implementation comes from "%s".)", script_path);
}

static MethodInfo info_from_utility_func(const StringName &p_function) {
	ERR_FAIL_COND_V(!Variant::has_utility_function(p_function), MethodInfo());

	MethodInfo info(p_function);

	if (Variant::has_utility_function_return_value(p_function)) {
		info.return_val.type = Variant::get_utility_function_return_type(p_function);
		if (info.return_val.type == Variant::NIL) {
			info.return_val.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		}
	}

	if (Variant::is_utility_function_vararg(p_function)) {
		info.flags |= METHOD_FLAG_VARARG;
	} else {
		for (int i = 0; i < Variant::get_utility_function_argument_count(p_function); i++) {
			PropertyInfo pi;
#ifdef DEBUG_ENABLED
			pi.name = Variant::get_utility_function_argument_name(p_function, i);
#else
			pi.name = "arg" + itos(i + 1);
#endif // DEBUG_ENABLED
			pi.type = Variant::get_utility_function_argument_type(p_function, i);
			info.arguments.push_back(pi);
		}
	}

	return info;
}

static GDScriptParser::DataType make_callable_type(const MethodInfo &p_info) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.builtin_type = Variant::CALLABLE;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	return type;
}

static GDScriptParser::DataType make_callable_type(const MethodInfo &p_info, const GDScriptParser::FunctionNode *p_function) {
	GDScriptParser::DataType type = make_callable_type(p_info);
	for (GDScriptParser::ParameterNode *parameter : p_function->parameters) {
		type.method_parameter_types.push_back(parameter->get_datatype());
	}
	type.method_return_type.push_back(p_function->get_datatype());
	return type;
}

static GDScriptParser::DataType make_signal_type(const MethodInfo &p_info) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.builtin_type = Variant::SIGNAL;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	return type;
}

static GDScriptParser::DataType make_signal_type(const MethodInfo &p_info, const GDScriptParser::SignalNode *p_signal) {
	GDScriptParser::DataType type = make_signal_type(p_info);
	for (const GDScriptParser::ParameterNode *parameter : p_signal->parameters) {
		type.method_parameter_types.push_back(parameter->get_datatype());
	}
	return type;
}

// Resolves a single hint type-name (as produced by the PROPERTY_HINT_CALLABLE_TYPE / ARRAY_TYPE grammar)
// into a leaf DataType. Returns false if the name cannot be resolved.
static bool _resolve_hint_leaf_type(const StringName &p_name, GDScriptParser::DataType &r_type) {
	r_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	r_type.is_constant = false;

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_name);
	if (builtin_type < Variant::VARIANT_MAX) {
		r_type.kind = GDScriptParser::DataType::BUILTIN;
		r_type.builtin_type = builtin_type;
		return true;
	}
	if (GDScriptAnalyzer::class_exists(p_name)) {
		r_type.kind = GDScriptParser::DataType::NATIVE;
		r_type.builtin_type = Variant::OBJECT;
		r_type.native_type = p_name;
		return true;
	}
	if (ScriptServer::is_global_class(p_name)) {
		Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(p_name));
		if (script.is_valid()) {
			r_type.kind = GDScriptParser::DataType::SCRIPT;
			r_type.builtin_type = Variant::OBJECT;
			r_type.native_type = script->get_instance_base_type();
			r_type.script_type = script;
			return true;
		}
	}
	return false;
}

// Splits a comma-separated list at bracket depth zero, so nested "Array[int]" / "Callable[[...]]"
// are not split internally. Trims surrounding whitespace on each element.
static Vector<String> _split_signature_top_level(const String &p_text) {
	Vector<String> parts;
	int depth = 0;
	int start = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t character = p_text[i];
		if (character == '[') {
			depth++;
		} else if (character == ']') {
			depth--;
		} else if (character == ',' && depth == 0) {
			parts.push_back(p_text.substr(start, i - start).strip_edges());
			start = i + 1;
		}
	}
	parts.push_back(p_text.substr(start).strip_edges());
	return parts;
}

static GDScriptParser::DataType _decode_signature_type(const String &p_encoded);

// Decodes a Callable/Signal signature suffix ("[[p0, p1], ret]" or "[[p0, p1]]") into r_type. Returns
// false — leaving r_type untouched (a bare callable/signal) — when the suffix does not match the
// expected grammar, so malformed external metadata degrades to gradual typing instead of a bogus
// (e.g. zero-argument void) signature that would wrongly reject valid calls.
static bool _decode_method_signature_suffix(const String &p_suffix, bool p_has_return, GDScriptParser::DataType &r_type) {
	// Validate the wrapper shape first: a well-formed suffix opens with the outer bracket plus the
	// params-block opener ("[["), closes on the outer bracket, and keeps brackets balanced throughout.
	if (!p_suffix.begins_with("[[") || !p_suffix.ends_with("]")) {
		return false;
	}
	int balance = 0;
	for (int i = 0; i < p_suffix.length(); i++) {
		if (p_suffix[i] == '[') {
			balance++;
		} else if (p_suffix[i] == ']') {
			balance--;
			if (balance < 0) {
				return false;
			}
		}
	}
	if (balance != 0) {
		return false;
	}

	const String inner = p_suffix.substr(1, p_suffix.length() - 2); // "[<params>], <ret>" or "[<params>]"

	// The parameter list is the first bracket-balanced "[...]" segment of `inner`.
	int depth = 0;
	int params_end = -1;
	for (int i = 0; i < inner.length(); i++) {
		if (inner[i] == '[') {
			depth++;
		} else if (inner[i] == ']') {
			depth--;
			if (depth == 0) {
				params_end = i;
				break;
			}
		}
	}
	if (params_end < 1) {
		return false;
	}

	Vector<GDScriptParser::DataType> parameter_types;
	const String params_block = inner.substr(1, params_end - 1); // between the inner brackets
	const String trimmed_params = params_block.strip_edges();
	if (!trimmed_params.is_empty()) {
		for (const String &parameter : _split_signature_top_level(params_block)) {
			if (parameter.is_empty()) {
				continue;
			}
			parameter_types.push_back(_decode_signature_type(parameter));
		}
	}

	Vector<GDScriptParser::DataType> return_types;
	if (p_has_return) {
		const String rest = inner.substr(params_end + 1).strip_edges(); // ", <ret>"
		const String return_name = rest.begins_with(",") ? rest.substr(1).strip_edges() : rest;
		GDScriptParser::DataType return_type;
		if (return_name.is_empty() || return_name == "void") {
			return_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
			return_type.kind = GDScriptParser::DataType::BUILTIN;
			return_type.builtin_type = Variant::NIL;
		} else {
			return_type = _decode_signature_type(return_name);
		}
		return_types.push_back(return_type);
	}

	r_type.has_method_signature = true;
	r_type.has_explicit_method_signature = true;
	r_type.method_parameter_types = parameter_types;
	r_type.method_return_type = return_types;

	// Mirror the rich slots into method_info too. Callable compatibility falls back to a MethodInfo
	// comparison when one side is MethodInfo-only (e.g. a utility-function reference like `sin` built by
	// make_callable_type); without this mirror a decoded explicit callable would carry an empty
	// MethodInfo and wrongly reject an otherwise-matching MethodInfo-only callable.
	MethodInfo signature_info;
	for (const GDScriptParser::DataType &parameter_type : parameter_types) {
		signature_info.arguments.push_back(parameter_type.to_property_info(""));
	}
	if (p_has_return && !return_types.is_empty()) {
		signature_info.return_val = return_types[0].to_property_info("");
	}
	r_type.method_info = signature_info;
	return true;
}

static GDScriptParser::DataType _decode_signature_type_base(const String &p_encoded) {
	GDScriptParser::DataType result;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	const String text = p_encoded.strip_edges();

	if (text == "Callable" || text.begins_with("Callable[")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::CALLABLE;
		if (text.length() > 8) { // has a "[...]" suffix after "Callable"
			_decode_method_signature_suffix(text.substr(8), true, result);
		}
		return result;
	}
	if (text == "Signal" || text.begins_with("Signal[")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::SIGNAL;
		if (text.length() > 6) {
			_decode_method_signature_suffix(text.substr(6), false, result);
		}
		return result;
	}
	if (text.begins_with("Array[") && text.ends_with("]")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::ARRAY;
		const String element = text.substr(6, text.length() - 7); // between "Array[" and trailing "]"
		GDScriptParser::DataType element_type = _decode_signature_type(element);
		element_type.is_constant = false;
		result.set_container_element_type(0, element_type);
		return result;
	}
	if (text.begins_with("Dictionary[") && text.ends_with("]")) {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::DICTIONARY;
		const String pair = text.substr(11, text.length() - 12); // between "Dictionary[" and trailing "]"
		const Vector<String> key_value = _split_signature_top_level(pair);
		if (key_value.size() == 2) {
			GDScriptParser::DataType key_type = _decode_signature_type(key_value[0]);
			GDScriptParser::DataType value_type = _decode_signature_type(key_value[1]);
			key_type.is_constant = false;
			value_type.is_constant = false;
			result.set_container_element_type(0, key_type);
			result.set_container_element_type(1, value_type);
		}
		return result;
	}
	if (_resolve_hint_leaf_type(text, result)) {
		return result;
	}
	// Unresolvable leaf: degrade to Variant rather than fail the whole decode.
	result.kind = GDScriptParser::DataType::VARIANT;
	return result;
}

static GDScriptParser::DataType _decode_signature_type(const String &p_encoded) {
	const String text = p_encoded.strip_edges();
	// A trailing `?` marks a nullable slot (encoded by _encode_signature_type). It only ever appears
	// as the final character of a whole type token; nested `T?` slots sit inside brackets and are
	// recovered by the recursive decode of each split element.
	if (text.ends_with("?")) {
		GDScriptParser::DataType result = _decode_signature_type_base(text.substr(0, text.length() - 1));
		if (result.kind != GDScriptParser::DataType::VARIANT) {
			result.is_nullable = true;
		}
		return result;
	}
	return _decode_signature_type_base(text);
}

// A signature slot is comparison-safe across the script-API boundary when its kind survives a
// PropertyInfo round-trip unambiguously. A user script/class surfaces as SCRIPT when rebuilt from
// PropertyInfo but may be a CLASS handle in a local annotation, and the strict rich-slot comparator
// keys on kind; enums and type parameters are likewise ambiguous. A signature carrying such a slot is
// kept non-explicit so the MethodInfo fallback — which compares object slots by class name — decides
// compatibility instead (mirroring how the encoder side suppresses these hints).
static bool _signature_slot_is_comparison_safe(const GDScriptParser::DataType &p_type) {
	switch (p_type.kind) {
		case GDScriptParser::DataType::SCRIPT:
		case GDScriptParser::DataType::CLASS:
		case GDScriptParser::DataType::ENUM:
		case GDScriptParser::DataType::TYPE_PARAMETER:
		case GDScriptParser::DataType::RESOLVING:
		case GDScriptParser::DataType::UNRESOLVED:
			return false;
		case GDScriptParser::DataType::BUILTIN:
			for (const GDScriptParser::DataType &element_type : p_type.container_element_types) {
				if (!_signature_slot_is_comparison_safe(element_type)) {
					return false;
				}
			}
			for (const GDScriptParser::DataType &parameter_type : p_type.method_parameter_types) {
				if (!_signature_slot_is_comparison_safe(parameter_type)) {
					return false;
				}
			}
			for (const GDScriptParser::DataType &return_type : p_type.method_return_type) {
				if (!_signature_slot_is_comparison_safe(return_type)) {
					return false;
				}
			}
			return true;
		case GDScriptParser::DataType::NATIVE:
		case GDScriptParser::DataType::VARIANT:
			return true;
	}
	return true;
}

static bool _signature_is_comparison_safe(const Vector<GDScriptParser::DataType> &p_parameter_types) {
	for (const GDScriptParser::DataType &parameter_type : p_parameter_types) {
		if (!_signature_slot_is_comparison_safe(parameter_type)) {
			return false;
		}
	}
	return true;
}

static GDScriptParser::DataType make_native_meta_type(const StringName &p_class_name) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_class_name;
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

static GDScriptParser::DataType make_script_meta_type(const Ref<Script> &p_script) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::SCRIPT;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_script->get_instance_base_type();
	type.script_type = p_script;
	type.script_path = p_script->get_path();
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

// In enum types, native_type is used to store the class (native or otherwise) that the enum belongs to.
// This disambiguates between similarly named enums in base classes or outer classes
static GDScriptParser::DataType make_enum_type(const StringName &p_enum_name, const String &p_base_name, const bool p_meta = false) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::ENUM;
	type.builtin_type = p_meta ? Variant::DICTIONARY : Variant::INT;
	type.enum_type = p_enum_name;
	type.is_constant = true;
	type.is_meta_type = p_meta;

	// For enums, native_type is only used to check compatibility in is_type_compatible()
	// We can set anything readable here for error messages, as long as it uniquely identifies the type of the enum
	if (p_base_name.is_empty()) {
		type.native_type = p_enum_name;
	} else {
		type.native_type = p_base_name + ENUM_SEPARATOR + p_enum_name;
	}

	return type;
}

static GDScriptParser::DataType make_class_enum_type(const StringName &p_enum_name, GDScriptParser::ClassNode *p_class, const String &p_script_path, bool p_meta = true) {
	GDScriptParser::DataType type = make_enum_type(p_enum_name, p_class->fqcn, p_meta);

	type.class_type = p_class;
	type.script_path = p_script_path;

	return type;
}

static GDScriptParser::DataType make_native_enum_type(const StringName &p_enum_name, const StringName &p_native_class, bool p_meta = true) {
	// Find out which base class declared the enum, so the name is always the same even when coming from other contexts.
	StringName native_base = p_native_class;
	while (true && native_base != StringName()) {
		if (ClassDB::has_enum(native_base, p_enum_name, true)) {
			break;
		}
		native_base = ClassDB::get_parent_class_nocheck(native_base);
	}

	GDScriptParser::DataType type = make_enum_type(p_enum_name, native_base, p_meta);
	if (p_meta) {
		// Native enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	List<StringName> enum_values;
	ClassDB::get_enum_constants(native_base, p_enum_name, &enum_values, true);

	for (const StringName &E : enum_values) {
		type.enum_values[E] = ClassDB::get_integer_constant(native_base, E);
	}

	return type;
}

static GDScriptParser::DataType make_builtin_enum_type(const StringName &p_enum_name, Variant::Type p_type, bool p_meta = true) {
	GDScriptParser::DataType type = make_enum_type(p_enum_name, Variant::get_type_name(p_type), p_meta);
	if (p_meta) {
		// Built-in enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	List<StringName> enum_values;
	Variant::get_enumerations_for_enum(p_type, p_enum_name, &enum_values);

	for (const StringName &E : enum_values) {
		type.enum_values[E] = Variant::get_enum_value(p_type, p_enum_name, E);
	}

	return type;
}

static GDScriptParser::DataType make_global_enum_type(const StringName &p_enum_name, const StringName &p_base, bool p_meta = true) {
	GDScriptParser::DataType type = make_enum_type(p_enum_name, p_base, p_meta);
	if (p_meta) {
		// Global enum types are not dictionaries.
		type.builtin_type = Variant::NIL;
		type.is_pseudo_type = true;
	}

	HashMap<StringName, int64_t> enum_values;
	CoreConstants::get_enum_values(type.native_type, &enum_values);
	for (const KeyValue<StringName, int64_t> &element : enum_values) {
		type.enum_values[element.key] = element.value;
	}

	return type;
}

static GDScriptParser::DataType make_builtin_meta_type(Variant::Type p_type) {
	GDScriptParser::DataType type;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.builtin_type = p_type;
	type.is_constant = true;
	type.is_meta_type = true;
	return type;
}

bool GDScriptAnalyzer::has_member_name_conflict_in_script_class(const StringName &p_member_name, const GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_member) {
	if (p_class->members_indices.has(p_member_name)) {
		int index = p_class->members_indices[p_member_name];
		const GDScriptParser::ClassNode::Member *member = &p_class->members[index];

		if (member->type == GDScriptParser::ClassNode::Member::VARIABLE ||
				member->type == GDScriptParser::ClassNode::Member::CONSTANT ||
				member->type == GDScriptParser::ClassNode::Member::ENUM ||
				member->type == GDScriptParser::ClassNode::Member::ENUM_VALUE ||
				member->type == GDScriptParser::ClassNode::Member::CLASS ||
				member->type == GDScriptParser::ClassNode::Member::SIGNAL) {
			return true;
		}
		if (p_member->type != GDScriptParser::Node::FUNCTION && member->type == GDScriptParser::ClassNode::Member::FUNCTION) {
			return true;
		}
	}

	return false;
}

bool GDScriptAnalyzer::has_member_name_conflict_in_native_type(const StringName &p_member_name, const StringName &p_native_type_string) {
	if (ClassDB::has_signal(p_native_type_string, p_member_name)) {
		return true;
	}
	if (ClassDB::has_property(p_native_type_string, p_member_name)) {
		return true;
	}
	if (ClassDB::has_integer_constant(p_native_type_string, p_member_name)) {
		return true;
	}
	if (p_member_name == CoreStringName(script)) {
		return true;
	}

	return false;
}

Error GDScriptAnalyzer::check_native_member_name_conflict(const StringName &p_member_name, const GDScriptParser::Node *p_member_node, const StringName &p_native_type_string) {
	if (has_member_name_conflict_in_native_type(p_member_name, p_native_type_string)) {
		push_error(vformat(R"(Member "%s" redefined (original in native class '%s'))", p_member_name, p_native_type_string), p_member_node);
		return ERR_PARSE_ERROR;
	}

	if (class_exists(p_member_name)) {
		push_error(vformat(R"(The member "%s" shadows a native class.)", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}

	if (GDScriptParser::get_builtin_type(p_member_name) < Variant::VARIANT_MAX) {
		push_error(vformat(R"(The member "%s" cannot have the same name as a builtin type.)", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}

	return OK;
}

Error GDScriptAnalyzer::check_class_member_name_conflict(const GDScriptParser::ClassNode *p_class_node, const StringName &p_member_name, const GDScriptParser::Node *p_member_node) {
	// TODO check outer classes for static members only
	const GDScriptParser::DataType *current_data_type = &p_class_node->base_type;
	while (current_data_type && current_data_type->kind == GDScriptParser::DataType::Kind::CLASS) {
		GDScriptParser::ClassNode *current_class_node = current_data_type->class_type;
		if (has_member_name_conflict_in_script_class(p_member_name, current_class_node, p_member_node)) {
			String parent_class_name = current_class_node->fqcn;
			if (current_class_node->identifier != nullptr) {
				parent_class_name = current_class_node->identifier->name;
			}
			push_error(vformat(R"(The member "%s" already exists in parent class %s.)", p_member_name, parent_class_name), p_member_node);
			return ERR_PARSE_ERROR;
		}
		current_data_type = &current_class_node->base_type;
	}

	// No need for native class recursion because Node exposes all Object's properties.
	if (current_data_type && current_data_type->kind == GDScriptParser::DataType::Kind::NATIVE) {
		if (current_data_type->native_type != StringName()) {
			return check_native_member_name_conflict(
					p_member_name,
					p_member_node,
					current_data_type->native_type);
		}
	}

	return OK;
}

void GDScriptAnalyzer::get_class_node_current_scope_classes(GDScriptParser::ClassNode *p_node, List<GDScriptParser::ClassNode *> *p_list, GDScriptParser::Node *p_source) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(p_list);

	if (p_list->find(p_node) != nullptr) {
		return;
	}

	p_list->push_back(p_node);

	// TODO: Try to solve class inheritance if not yet resolving.

	// Prioritize node base type over its outer class
	if (p_node->base_type.class_type != nullptr) {
		// TODO: 'ensure_cached_external_parser_for_class()' is only necessary because 'resolve_class_inheritance()' is not getting called here.
		ensure_cached_external_parser_for_class(p_node->base_type.class_type, p_node, "Trying to fetch classes in the current scope", p_source);
		get_class_node_current_scope_classes(p_node->base_type.class_type, p_list, p_source);
	}

	if (p_node->outer != nullptr) {
		// TODO: 'ensure_cached_external_parser_for_class()' is only necessary because 'resolve_class_inheritance()' is not getting called here.
		ensure_cached_external_parser_for_class(p_node->outer, p_node, "Trying to fetch classes in the current scope", p_source);
		get_class_node_current_scope_classes(p_node->outer, p_list, p_source);
	}
}

Error GDScriptAnalyzer::resolve_class_inheritance(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<GDScriptParserRef> parser_ref = ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class inheritance", p_source);
	Finally finally([&]() {
		for (GDScriptParser::ClassNode *look_class = p_class; look_class != nullptr; look_class = look_class->base_type.class_type) {
			ensure_cached_external_parser_for_class(look_class->base_type.class_type, look_class, "Trying to resolve class inheritance", p_source);
		}
	});

	if (p_class->base_type.is_resolving()) {
		push_error(vformat(R"(Could not resolve class "%s": Cyclic reference.)", type_from_metatype(p_class->get_datatype()).to_string()), p_source);
		return ERR_PARSE_ERROR;
	}

	if (!p_class->base_type.has_no_type()) {
		// Already resolved.
		return OK;
	}

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			// Error already pushed.
			return ERR_PARSE_ERROR;
		}

		Error err = parser_ref->raise_status(GDScriptParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
			return ERR_PARSE_ERROR;
		}

		GDScriptAnalyzer *other_analyzer = parser_ref->get_analyzer();
		GDScriptParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_inheritance(p_class);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve inheritance for class "%s".)", p_class->fqcn), p_source);
			return ERR_PARSE_ERROR;
		}

		return OK;
	}

	GDScriptParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	if (p_class->identifier) {
		StringName class_name = p_class->identifier->name;
		StringName global_class_name = (p_class == parser->head && !p_class->qualified_global_name.is_empty()) ? StringName(p_class->qualified_global_name) : class_name;
		if (GDScriptParser::get_builtin_type(class_name) < Variant::VARIANT_MAX) {
			push_error(vformat(R"(Class "%s" hides a built-in type.)", class_name), p_class->identifier);
		} else if (class_exists(class_name)) {
			push_error(vformat(R"(Class "%s" hides a native class.)", class_name), p_class->identifier);
		} else if (ScriptServer::is_global_class(global_class_name) && (!GDScript::is_canonically_equal_paths(ScriptServer::get_global_class_path(global_class_name), parser->script_path) || p_class != parser->head)) {
			push_error(vformat(R"(Class "%s" from "%s" collides with global script class from "%s".)", global_class_name, parser->script_path, ScriptServer::get_global_class_path(global_class_name)), p_class->identifier);
		} else if (ProjectSettings::get_singleton()->has_autoload(class_name) && ProjectSettings::get_singleton()->get_autoload(class_name).is_singleton) {
			push_error(vformat(R"(Class "%s" hides an autoload singleton.)", class_name), p_class->identifier);
		}
	}

	GDScriptParser::DataType resolving_datatype;
	resolving_datatype.kind = GDScriptParser::DataType::RESOLVING;
	p_class->base_type = resolving_datatype;

	// Set datatype for class.
	GDScriptParser::DataType class_type;
	class_type.is_constant = true;
	class_type.is_meta_type = true;
	class_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	class_type.kind = GDScriptParser::DataType::CLASS;
	class_type.class_type = p_class;
	class_type.script_path = parser->script_path;
	class_type.builtin_type = Variant::OBJECT;
	p_class->set_datatype(class_type);

	GDScriptParser::DataType result;
	// Tracks which extends type arguments failed to resolve, so the deferred bound check below skips
	// them and does not emit a second diagnostic for an already-reported argument.
	Vector<bool> extends_argument_failed;
	if (!p_class->extends_used) {
		result.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
		result.kind = GDScriptParser::DataType::NATIVE;
		result.builtin_type = Variant::OBJECT;
		result.native_type = SNAME("RefCounted");
	} else {
		result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

		GDScriptParser::DataType base;

		int extends_index = 0;

		if (!p_class->extends_path.is_empty()) {
			if (p_class->extends_path.is_relative_path()) {
				p_class->extends_path = class_type.script_path.get_base_dir().path_join(p_class->extends_path).simplify_path();
			}
			Ref<GDScriptParserRef> ext_parser = parser->get_depended_parser_for(p_class->extends_path);
			if (ext_parser.is_null()) {
				push_error(vformat(R"(Could not resolve super class path "%s".)", p_class->extends_path), p_class);
				return ERR_PARSE_ERROR;
			}

			Error err = ext_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
			if (err != OK) {
				push_error(vformat(R"(Could not resolve super class inheritance from "%s".)", p_class->extends_path), p_class);
				return err;
			}

#ifdef DEBUG_ENABLED
			if (!parser->_is_tool && ext_parser->get_parser()->_is_tool) {
				parser->push_warning(p_class, GDScriptWarning::MISSING_TOOL);
			}
#endif // DEBUG_ENABLED

			base = ext_parser->get_parser()->head->get_datatype();
		} else {
			if (p_class->extends.is_empty()) {
				push_error("Could not resolve an empty super class path.", p_class);
				return ERR_PARSE_ERROR;
			}
			GDScriptParser::IdentifierNode *id = p_class->extends[extends_index++];
			const StringName &name = id->name;
			base.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

			if (ScriptServer::is_global_class(name)) {
				String base_path = ScriptServer::get_global_class_path(name);

				if (GDScript::is_canonically_equal_paths(base_path, parser->script_path)) {
					base = parser->head->get_datatype();
				} else {
					Ref<GDScriptParserRef> base_parser = parser->get_depended_parser_for(base_path);
					if (base_parser.is_null()) {
						push_error(vformat(R"(Could not resolve super class "%s".)", name), id);
						return ERR_PARSE_ERROR;
					}

					Error err = base_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
					if (err != OK) {
						push_error(vformat(R"(Could not resolve super class inheritance from "%s".)", name), id);
						return err;
					}

#ifdef DEBUG_ENABLED
					if (!parser->_is_tool && base_parser->get_parser()->_is_tool) {
						parser->push_warning(p_class, GDScriptWarning::MISSING_TOOL);
					}
#endif // DEBUG_ENABLED

					base = base_parser->get_parser()->head->get_datatype();
				}
			} else if (ProjectSettings::get_singleton()->has_autoload(name) && ProjectSettings::get_singleton()->get_autoload(name).is_singleton && !GDScriptLanguage::get_singleton()->is_reserved_global_name(name)) {
				// A reserved named global (e.g. the `godot` reflection namespace) is not a
				// base type; an autoload of that name must not be used for `extends`.
				const ProjectSettings::AutoloadInfo &info = ProjectSettings::get_singleton()->get_autoload(name);
				if (!info.path.has_extension(GDScriptLanguage::get_singleton()->get_extension())) {
					push_error(vformat(R"(Singleton %s is not a GDScript.)", info.name), id);
					return ERR_PARSE_ERROR;
				}

				Ref<GDScriptParserRef> info_parser = parser->get_depended_parser_for(info.path);
				if (info_parser.is_null()) {
					push_error(vformat(R"(Could not parse singleton from "%s".)", info.path), id);
					return ERR_PARSE_ERROR;
				}

				Error err = info_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
				if (err != OK) {
					push_error(vformat(R"(Could not resolve super class inheritance from "%s".)", name), id);
					return err;
				}

#ifdef DEBUG_ENABLED
				if (!parser->_is_tool && info_parser->get_parser()->_is_tool) {
					parser->push_warning(p_class, GDScriptWarning::MISSING_TOOL);
				}
#endif // DEBUG_ENABLED

				base = info_parser->get_parser()->head->get_datatype();
			} else if (class_exists(name)) {
				if (Engine::get_singleton()->has_singleton(name)) {
					push_error(vformat(R"(Cannot inherit native class "%s" because it is an engine singleton.)", name), id);
					return ERR_PARSE_ERROR;
				}
				base.kind = GDScriptParser::DataType::NATIVE;
				base.builtin_type = Variant::OBJECT;
				base.native_type = name;
			} else {
				// Look for other classes in script.
				bool found = false;
				List<GDScriptParser::ClassNode *> script_classes;
				get_class_node_current_scope_classes(p_class, &script_classes, id);
				for (GDScriptParser::ClassNode *look_class : script_classes) {
					if (look_class->identifier && look_class->identifier->name == name) {
						if (!look_class->get_datatype().is_set()) {
							Error err = resolve_class_inheritance(look_class, id);
							if (err) {
								return err;
							}
						}
						base = look_class->get_datatype();
						found = true;
						break;
					}
					if (look_class->has_member(name)) {
						resolve_class_member(look_class, name, id);
						GDScriptParser::ClassNode::Member member = look_class->get_member(name);
						GDScriptParser::DataType member_datatype = member.get_datatype();

						switch (member.type) {
							case GDScriptParser::ClassNode::Member::CLASS:
								break; // OK.
							case GDScriptParser::ClassNode::Member::CONSTANT:
								if (member_datatype.kind != GDScriptParser::DataType::SCRIPT && member_datatype.kind != GDScriptParser::DataType::CLASS) {
									push_error(vformat(R"(Constant "%s" is not a preloaded script or class.)", name), id);
									return ERR_PARSE_ERROR;
								}
								break;
							default:
								push_error(vformat(R"(Cannot use %s "%s" in extends chain.)", member.get_type_name(), name), id);
								return ERR_PARSE_ERROR;
						}

						base = member_datatype;
						found = true;
						break;
					}
				}

				if (!found) {
					push_error(vformat(R"(Could not find base class "%s".)", name), id);
					return ERR_PARSE_ERROR;
				}
			}
		}

		for (int index = extends_index; index < p_class->extends.size(); index++) {
			GDScriptParser::IdentifierNode *id = p_class->extends[index];

			if (base.kind != GDScriptParser::DataType::CLASS) {
				push_error(vformat(R"(Cannot get nested types for extension from non-GDScript type "%s".)", base.to_string()), id);
				return ERR_PARSE_ERROR;
			}

			reduce_identifier_from_base(id, &base);
			GDScriptParser::DataType id_type = id->get_datatype();

			if (!id_type.is_set()) {
				push_error(vformat(R"(Could not find nested type "%s".)", id->name), id);
				return ERR_PARSE_ERROR;
			} else if (id_type.kind != GDScriptParser::DataType::SCRIPT && id_type.kind != GDScriptParser::DataType::CLASS) {
				push_error(vformat(R"(Identifier "%s" is not a preloaded script or class.)", id->name), id);
				return ERR_PARSE_ERROR;
			}

			base = id_type;
		}

		result = base;

		// Specialize a generic base, e.g. `Stack[T] extends List[T]` or `extends List[int]`. The
		// arguments are resolved in this class's scope so a child parameter like `T` binds here.
		if (!p_class->extends_type_arguments.is_empty()) {
			if (result.kind != GDScriptParser::DataType::CLASS || result.class_type == nullptr) {
				push_error(vformat(R"(Type "%s" is not a generic class and cannot take type arguments.)", result.to_string()), p_class->extends_type_arguments[0]);
				return ERR_PARSE_ERROR;
			}
			// Bind the type arguments now but defer their bound validation until the specialized base
			// is installed below. A self-referential argument (`class Sword extends Box[Sword]`, the
			// F-bounded/CRTP pattern) must be checked against the base's bound by walking
			// `Sword -> Box -> ...`; doing that while the base is unset re-enters Sword's still
			// in-progress inheritance resolution and reports a spurious cyclic reference. Deferring also
			// avoids any reentrant check observing a half-resolved (unspecialized) base.
			if (!apply_class_type_arguments(result, p_class->extends_type_arguments, p_class->extends_type_arguments[0], /* check_bounds */ false, &extends_argument_failed)) {
				return ERR_PARSE_ERROR;
			}
		}
	}

	if (!result.is_set() || result.has_no_type()) {
		// TODO: More specific error messages.
		push_error(vformat(R"(Could not resolve inheritance for class "%s".)", p_class->identifier == nullptr ? "<main>" : p_class->identifier->name), p_class);
		return ERR_PARSE_ERROR;
	}

	if (result.kind == GDScriptParser::DataType::CLASS && result.class_type != nullptr && result.class_type->is_trait) {
		const String class_name = _class_or_trait_name(p_class);
		const String trait_name = _class_or_trait_name(result.class_type);
		const GDScriptParser::Node *source = p_class->extends.is_empty() ? static_cast<const GDScriptParser::Node *>(p_class) : p_class->extends[0];
		push_error(vformat(R"(Class "%s" cannot extend trait "%s"; use "uses %s" instead.)", class_name, trait_name, trait_name), source);
		return ERR_PARSE_ERROR;
	}

	// Check for cyclic inheritance.
	const GDScriptParser::ClassNode *base_class = result.class_type;
	while (base_class) {
		if (base_class->fqcn == p_class->fqcn) {
			push_error("Cyclic inheritance.", p_class);
			return ERR_PARSE_ERROR;
		}
		base_class = base_class->base_type.class_type;
	}

	p_class->base_type = result;
	class_type.native_type = result.native_type;
	p_class->set_datatype(class_type);

	// Validate the deferred type-argument bounds now that the specialized base is installed, so a
	// self-referential argument (`class Sword extends Box[Sword]`) is checked against the real,
	// fully-resolved inheritance chain rather than re-entering this class's own resolution.
	if (!p_class->extends_type_arguments.is_empty()) {
		Vector<const GDScriptParser::Node *> argument_sources;
		for (GDScriptParser::TypeNode *argument_node : p_class->extends_type_arguments) {
			argument_sources.push_back(argument_node);
		}
		if (!check_class_type_argument_bounds(p_class->base_type, extends_argument_failed, argument_sources)) {
			return ERR_PARSE_ERROR;
		}
	}

	// Apply annotations.
	for (GDScriptParser::AnnotationNode *&E : p_class->annotations) {
		resolve_annotation(E);
		E->apply(parser, p_class, p_class->outer);
	}

	parser->current_class = previous_class;

	return OK;
}

Error GDScriptAnalyzer::resolve_class_inheritance(GDScriptParser::ClassNode *p_class, bool p_recursive) {
	Error err = resolve_class_inheritance(p_class);
	if (err) {
		return err;
	}

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			if (p_class->members[i].type == GDScriptParser::ClassNode::Member::CLASS) {
				err = resolve_class_inheritance(p_class->members[i].m_class, true);
				if (err) {
					return err;
				}
			}
		}
	}

	return OK;
}

void GDScriptAnalyzer::resolve_function_signature_in_class(GDScriptParser::FunctionNode *p_function,
		GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source) {
	GDScriptParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;
	resolve_function_signature(p_function, p_source);
	parser->current_class = previous_class;
}

GDScriptParser::DataType GDScriptAnalyzer::resolve_datatype(GDScriptParser::TypeNode *p_type) {
	GDScriptParser::DataType bad_type;
	bad_type.kind = GDScriptParser::DataType::VARIANT;
	bad_type.type_source = GDScriptParser::DataType::INFERRED;

	if (p_type == nullptr) {
		return bad_type;
	}

	if (p_type->get_datatype().is_resolving()) {
		push_error(R"(Could not resolve datatype: Cyclic reference.)", p_type);
		return bad_type;
	}

	if (!p_type->get_datatype().has_no_type()) {
		return p_type->get_datatype();
	}

	GDScriptParser::DataType resolving_datatype;
	resolving_datatype.kind = GDScriptParser::DataType::RESOLVING;
	p_type->set_datatype(resolving_datatype);

	GDScriptParser::DataType result;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	auto finalize_datatype = [&](GDScriptParser::DataType p_result) -> GDScriptParser::DataType {
		if (p_type->is_nullable && !p_result.is_variant() && !(p_result.kind == GDScriptParser::DataType::BUILTIN && p_result.builtin_type == Variant::NIL)) {
			p_result.is_nullable = true;
		}
		p_type->set_datatype(p_result);
		return p_result;
	};

	if (p_type->type_chain.is_empty()) {
		// void.
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::NIL;
		return finalize_datatype(result);
	}

	const GDScriptParser::IdentifierNode *first_id = p_type->type_chain[0];
	StringName first = first_id->name;
	bool type_found = false;
	int resolved_type_chain_size = 1;

	// Type parameters of the enclosing generic class or method shadow any other name, so a bare
	// `T` resolves to the parameter handle before falling back to the normal type lookup below.
	{
		GDScriptParser::DataType type_parameter;
		if (resolve_type_parameter(first, type_parameter)) {
			if (p_type->type_chain.size() > 1) {
				push_error(vformat(R"(Type parameter "%s" does not contain nested types.)", first), p_type->type_chain[1]);
				return bad_type;
			}
			if (!p_type->container_types.is_empty()) {
				push_error(vformat(R"(Type parameter "%s" cannot be specialized with type arguments.)", first), p_type);
				return bad_type;
			}
			return finalize_datatype(type_parameter);
		}
	}

	if (first_id->suite && first_id->suite->has_local(first)) {
		const GDScriptParser::SuiteNode::Local &local = first_id->suite->get_local(first);
		if (local.type == GDScriptParser::SuiteNode::Local::CONSTANT) {
			result = local.get_datatype();
			if (!result.is_set()) {
				// Don't try to resolve it as the constant can be declared below.
				push_error(vformat(R"(Local constant "%s" is not resolved at this point.)", first), first_id);
				return bad_type;
			}
			if (result.is_meta_type) {
				type_found = true;
			} else if (Ref<Script>(local.constant->initializer->reduced_value).is_valid()) {
				Ref<GDScript> gdscript = local.constant->initializer->reduced_value;
				if (gdscript.is_valid()) {
					Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(gdscript->get_script_path());
					if (ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED) != OK) {
						push_error(vformat(R"(Could not parse script from "%s".)", gdscript->get_script_path()), first_id);
						return bad_type;
					}
					result = ref->get_parser()->head->get_datatype();
				} else {
					result = make_script_meta_type(local.constant->initializer->reduced_value);
				}
				type_found = true;
			} else {
				push_error(vformat(R"(Local constant "%s" is not a valid type.)", first), first_id);
				return bad_type;
			}
		} else {
			push_error(vformat(R"(Local %s "%s" cannot be used as a type.)", local.get_name(), first), first_id);
			return bad_type;
		}
	}

	if (!type_found) {
		if (first == SNAME("Variant")) {
			if (p_type->type_chain.size() == 2) {
				// May be nested enum.
				const StringName enum_name = p_type->type_chain[1]->name;
				const StringName qualified_name = String(first) + ENUM_SEPARATOR + String(p_type->type_chain[1]->name);
				if (CoreConstants::is_global_enum(qualified_name)) {
					result = make_global_enum_type(enum_name, first, true);
					return finalize_datatype(result);
				} else {
					push_error(vformat(R"(Name "%s" is not a nested type of "Variant".)", enum_name), p_type->type_chain[1]);
					return bad_type;
				}
			} else if (p_type->type_chain.size() > 2) {
				push_error(R"(Variant only contains enum types, which do not have nested types.)", p_type->type_chain[2]);
				return bad_type;
			}
			result.kind = GDScriptParser::DataType::VARIANT;
		} else if (GDScriptParser::get_builtin_type(first) < Variant::VARIANT_MAX) {
			// Built-in types.
			const Variant::Type builtin_type = GDScriptParser::get_builtin_type(first);

			if (p_type->type_chain.size() == 2) {
				// May be nested enum.
				const StringName enum_name = p_type->type_chain[1]->name;
				if (Variant::has_enum(builtin_type, enum_name)) {
					result = make_builtin_enum_type(enum_name, builtin_type, true);
					return finalize_datatype(result);
				} else {
					push_error(vformat(R"(Name "%s" is not a nested type of "%s".)", enum_name, first), p_type->type_chain[1]);
					return bad_type;
				}
			} else if (p_type->type_chain.size() > 2) {
				push_error(R"(Built-in types only contain enum types, which do not have nested types.)", p_type->type_chain[2]);
				return bad_type;
			}

			result.kind = GDScriptParser::DataType::BUILTIN;
			result.builtin_type = builtin_type;

			if (builtin_type == Variant::CALLABLE || builtin_type == Variant::SIGNAL) {
				if (p_type->has_signature) {
					result.has_method_signature = true;
					result.has_explicit_method_signature = true;
					MethodInfo method_info;
					for (int i = 0; i < p_type->signature_parameter_types.size(); i++) {
						GDScriptParser::DataType parameter_type = type_from_metatype(resolve_datatype(p_type->signature_parameter_types[i]));
						result.method_parameter_types.push_back(parameter_type);
						method_info.arguments.push_back(parameter_type.to_property_info(""));
					}
					if (builtin_type == Variant::CALLABLE) {
						GDScriptParser::DataType return_type = type_from_metatype(resolve_datatype(p_type->signature_return_type));
						result.method_return_type.push_back(return_type);
						method_info.return_val = return_type.to_property_info("");
					}
					result.method_info = method_info;
				}
			}
			if (builtin_type == Variant::ARRAY) {
				GDScriptParser::DataType container_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
				if (container_type.kind != GDScriptParser::DataType::VARIANT) {
					container_type.is_constant = false;
					result.set_container_element_type(0, container_type);
				}
			}
			if (builtin_type == Variant::DICTIONARY) {
				GDScriptParser::DataType key_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
				if (key_type.kind != GDScriptParser::DataType::VARIANT) {
					key_type.is_constant = false;
					result.set_container_element_type(0, key_type);
				}
				GDScriptParser::DataType value_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(1)));
				if (value_type.kind != GDScriptParser::DataType::VARIANT) {
					value_type.is_constant = false;
					result.set_container_element_type(1, value_type);
				}
			}
		} else if (class_exists(first)) {
			// Native engine classes.
			result.kind = GDScriptParser::DataType::NATIVE;
			result.builtin_type = Variant::OBJECT;
			result.native_type = first;
		} else {
			bool current_scope_has_name = false;
			List<GDScriptParser::ClassNode *> script_classes;
			get_class_node_current_scope_classes(parser->current_class, &script_classes, p_type);
			for (GDScriptParser::ClassNode *script_class : script_classes) {
				if ((script_class->identifier && script_class->identifier->name == first) || script_class->members_indices.has(first)) {
					current_scope_has_name = true;
					break;
				}
			}

			if (!current_scope_has_name) {
				StringName namespace_global_class;
				bool namespace_error = false;
				int namespace_type_chain_size = 0;
				if (get_namespace_global_class_from_type_chain(p_type->type_chain, p_type, namespace_global_class, namespace_type_chain_size, namespace_error)) {
					if (namespace_error) {
						return bad_type;
					}
					result = make_global_class_meta_type(namespace_global_class, p_type);
					resolved_type_chain_size = namespace_type_chain_size;
				}
			}
		}

		if (result.is_set()) {
			// Found.
		} else if (ScriptServer::is_global_class(first)) {
			if (GDScript::is_canonically_equal_paths(parser->script_path, ScriptServer::get_global_class_path(first))) {
				result = parser->head->get_datatype();
			} else {
				String path = ScriptServer::get_global_class_path(first);
				String ext = path.get_extension();
				if (ext == GDScriptLanguage::get_singleton()->get_extension()) {
					Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(path);
					if (ref.is_null() || ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED) != OK) {
						push_error(vformat(R"(Could not parse global class "%s" from "%s".)", first, ScriptServer::get_global_class_path(first)), p_type);
						return bad_type;
					}
					result = ref->get_parser()->head->get_datatype();
				} else {
					result = make_script_meta_type(ResourceLoader::load(path, "Script"));
				}
			}
		} else if (ProjectSettings::get_singleton()->has_autoload(first) && ProjectSettings::get_singleton()->get_autoload(first).is_singleton && !GDScriptLanguage::get_singleton()->is_reserved_global_name(first)) {
			// A reserved named global (e.g. the `godot` reflection namespace) is not a type;
			// an autoload of that name must not be resolved as one in a type position.
			const ProjectSettings::AutoloadInfo &autoload = ProjectSettings::get_singleton()->get_autoload(first);
			String script_path;
			if (ResourceLoader::get_resource_type(autoload.path) == "PackedScene") {
				// Try to get script from scene if possible.
				if (GDScriptLanguage::get_singleton()->has_any_global_constant(autoload.name)) {
					Variant constant = GDScriptLanguage::get_singleton()->get_any_global_constant(autoload.name);
					Node *node = Object::cast_to<Node>(constant);
					if (node != nullptr) {
						Ref<GDScript> scr = node->get_script();
						if (scr.is_valid()) {
							script_path = scr->get_script_path();
						}
					}
				}
			} else if (ResourceLoader::get_resource_type(autoload.path) == "GDScript") {
				script_path = autoload.path;
			}
			if (script_path.is_empty()) {
				return bad_type;
			}
			Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(script_path);
			if (ref.is_null()) {
				push_error(vformat(R"(The referenced autoload "%s" (from "%s") could not be loaded.)", first, script_path), p_type);
				return bad_type;
			}
			if (ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED) != OK) {
				push_error(vformat(R"(Could not parse singleton "%s" from "%s".)", first, script_path), p_type);
				return bad_type;
			}
			result = ref->get_parser()->head->get_datatype();
		} else if (ClassDB::has_enum(parser->current_class->base_type.native_type, first)) {
			// Native enum in current class.
			result = make_native_enum_type(first, parser->current_class->base_type.native_type);
		} else if (CoreConstants::is_global_enum(first)) {
			if (p_type->type_chain.size() > 1) {
				push_error(R"(Enums cannot contain nested types.)", p_type->type_chain[1]);
				return bad_type;
			}
			result = make_global_enum_type(first, StringName());
		} else {
			// Classes in current scope.
			List<GDScriptParser::ClassNode *> script_classes;
			bool found = false;
			get_class_node_current_scope_classes(parser->current_class, &script_classes, p_type);
			for (GDScriptParser::ClassNode *script_class : script_classes) {
				if (found) {
					break;
				}

				if (script_class->identifier && script_class->identifier->name == first) {
					result = script_class->get_datatype();
					break;
				}
				if (script_class->members_indices.has(first)) {
					resolve_class_member(script_class, first, p_type);

					GDScriptParser::ClassNode::Member member = script_class->get_member(first);
					switch (member.type) {
						case GDScriptParser::ClassNode::Member::CLASS:
							result = member.get_datatype();
							found = true;
							break;
						case GDScriptParser::ClassNode::Member::ENUM:
							result = member.get_datatype();
							found = true;
							break;
						case GDScriptParser::ClassNode::Member::CONSTANT:
							if (member.get_datatype().is_meta_type) {
								result = member.get_datatype();
								found = true;
								break;
							} else if (Ref<Script>(member.constant->initializer->reduced_value).is_valid()) {
								Ref<GDScript> gdscript = member.constant->initializer->reduced_value;
								if (gdscript.is_valid()) {
									Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(gdscript->get_script_path());
									if (ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED) != OK) {
										push_error(vformat(R"(Could not parse script from "%s".)", gdscript->get_script_path()), p_type);
										return bad_type;
									}
									result = ref->get_parser()->head->get_datatype();
								} else {
									result = make_script_meta_type(member.constant->initializer->reduced_value);
								}
								found = true;
								break;
							}
							[[fallthrough]];
						default:
							push_error(vformat(R"("%s" is a %s but does not contain a type.)", first, member.get_type_name()), p_type);
							return bad_type;
					}
				}
			}
		}
	}

	if (!result.is_set()) {
		push_error(vformat(R"(Could not find type "%s" in the current scope.)", first), p_type);
		return bad_type;
	}

	if (p_type->type_chain.size() > resolved_type_chain_size) {
		if (result.kind == GDScriptParser::DataType::CLASS) {
			for (int i = resolved_type_chain_size; i < p_type->type_chain.size(); i++) {
				GDScriptParser::DataType base = result;
				reduce_identifier_from_base(p_type->type_chain[i], &base);
				result = p_type->type_chain[i]->get_datatype();
				if (!result.is_set()) {
					push_error(vformat(R"(Could not find type "%s" under base "%s".)", p_type->type_chain[i]->name, base.to_string()), p_type->type_chain[resolved_type_chain_size]);
					return bad_type;
				} else if (!result.is_meta_type) {
					push_error(vformat(R"(Member "%s" under base "%s" is not a valid type.)", p_type->type_chain[i]->name, base.to_string()), p_type->type_chain[resolved_type_chain_size]);
					return bad_type;
				}
			}
		} else if (result.kind == GDScriptParser::DataType::NATIVE) {
			// Only enums allowed for native.
			if (ClassDB::has_enum(result.native_type, p_type->type_chain[resolved_type_chain_size]->name)) {
				if (p_type->type_chain.size() > resolved_type_chain_size + 1) {
					push_error(R"(Enums cannot contain nested types.)", p_type->type_chain[resolved_type_chain_size + 1]);
					return bad_type;
				} else {
					result = make_native_enum_type(p_type->type_chain[resolved_type_chain_size]->name, result.native_type);
				}
			} else {
				push_error(vformat(R"(Could not find type "%s" in "%s".)", p_type->type_chain[resolved_type_chain_size]->name, first), p_type->type_chain[resolved_type_chain_size]);
				return bad_type;
			}
		} else {
			push_error(vformat(R"(Could not find nested type "%s" under base "%s".)", p_type->type_chain[resolved_type_chain_size]->name, result.to_string()), p_type->type_chain[resolved_type_chain_size]);
			return bad_type;
		}
	}

	if (!p_type->container_types.is_empty()) {
		if (result.builtin_type == Variant::ARRAY) {
			if (p_type->container_types.size() != 1) {
				push_error(R"(Typed arrays require exactly one collection element type.)", p_type);
				return bad_type;
			}
		} else if (result.builtin_type == Variant::DICTIONARY) {
			if (p_type->container_types.size() != 2) {
				push_error(R"(Typed dictionaries require exactly two collection element types.)", p_type);
				return bad_type;
			}
		} else if (result.kind == GDScriptParser::DataType::CLASS && result.class_type != nullptr) {
			// Generic class specialization, e.g. `Box[int]`. The brackets carry type arguments
			// rather than collection element types, so they must match the class's parameter list.
			if (!apply_class_type_arguments(result, p_type->container_types, p_type)) {
				return bad_type;
			}
		} else {
			push_error(R"(Only arrays and dictionaries can specify collection element types.)", p_type);
			return bad_type;
		}
	}

	return finalize_datatype(result);
}

bool GDScriptAnalyzer::resolve_type_parameter(const StringName &p_name, GDScriptParser::DataType &r_type) {
	const GDScriptParser::TypeParameterNode *parameter = nullptr;
	GDScriptParser::DataType::TypeParameterScope scope = GDScriptParser::DataType::TYPE_PARAMETER_NONE;
	int index = -1;

	auto match_in = [&](const Vector<GDScriptParser::TypeParameterNode *> &p_parameters, GDScriptParser::DataType::TypeParameterScope p_scope) -> bool {
		for (int i = 0; i < p_parameters.size(); i++) {
			const GDScriptParser::TypeParameterNode *candidate = p_parameters[i];
			if (candidate != nullptr && candidate->identifier != nullptr && candidate->identifier->name == p_name) {
				parameter = candidate;
				scope = p_scope;
				index = i;
				return true;
			}
		}
		return false;
	};

	GDScriptParser::ClassNode *declaring_class = nullptr;

	// Method type parameters shadow class ones, and inner classes shadow their outer classes. A
	// lambda body is analyzed with `current_function` set to the lambda's own function, which has no
	// type parameters, so walk out through each enclosing lambda to its parent function to keep the
	// surrounding generic method's parameters visible.
	bool found_method_parameter = false;
	for (GDScriptParser::FunctionNode *enclosing = parser->current_function; enclosing != nullptr;) {
		if (match_in(enclosing->type_parameters, GDScriptParser::DataType::TYPE_PARAMETER_METHOD)) {
			found_method_parameter = true;
			break;
		}
		enclosing = enclosing->source_lambda != nullptr ? enclosing->source_lambda->parent_function : nullptr;
	}
	if (found_method_parameter) {
		// Found a method type parameter.
	} else {
		for (GDScriptParser::ClassNode *script_class = parser->current_class; script_class != nullptr; script_class = script_class->outer) {
			if (match_in(script_class->type_parameters, GDScriptParser::DataType::TYPE_PARAMETER_CLASS)) {
				declaring_class = script_class;
				break;
			}
		}
	}

	if (parameter == nullptr) {
		return false;
	}

	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::TYPE_PARAMETER;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_name = p_name;
	type.type_parameter_scope = scope;
	type.type_parameter_index = index;
	if (parameter->bound != nullptr) {
		// A class type parameter's bound belongs to its declaring class, not wherever the parameter is
		// used. Resolve (and thus cache) it in that scope so an enclosing method type parameter cannot
		// shadow the bound name and poison the cached datatype for later uses.
		GDScriptParser::ClassNode *previous_class = parser->current_class;
		GDScriptParser::FunctionNode *previous_function = parser->current_function;
		if (scope == GDScriptParser::DataType::TYPE_PARAMETER_CLASS && declaring_class != nullptr) {
			parser->current_class = declaring_class;
			parser->current_function = nullptr;
		}
		type.type_parameter_bound.push_back(type_from_metatype(resolve_datatype(parameter->bound)));
		parser->current_class = previous_class;
		parser->current_function = previous_function;
	}

	r_type = type;
	return true;
}

GDScriptParser::FunctionNode *GDScriptAnalyzer::find_generic_method(GDScriptParser::ClassNode *p_class, const StringName &p_name, bool &r_found_member) {
	// Find a generic method named `p_name` reachable from `p_class`, matching get_function_signature's
	// resolution order: the entire class/base chain is searched for an own member first, and only if
	// none is found are the starting class's applied traits consulted (base-class traits are already
	// flattened into the base members walked above). `r_found_member` reports whether any member of
	// that name exists, so callers can tell a missing method from a non-generic one.
	r_found_member = false;
	for (GDScriptParser::ClassNode *lookup_class = p_class; lookup_class != nullptr; lookup_class = lookup_class->base_type.class_type) {
		if (lookup_class->has_member(p_name)) {
			r_found_member = true;
			const GDScriptParser::ClassNode::Member &member = lookup_class->get_member(p_name);
			if (member.type == GDScriptParser::ClassNode::Member::FUNCTION && member.function != nullptr && !member.function->type_parameters.is_empty()) {
				return member.function;
			}
			return nullptr;
		}
	}
	if (p_class != nullptr && (p_class->is_trait || !p_class->used_traits.is_empty())) {
		resolve_trait_uses(p_class);
		for (GDScriptParser::ClassNode *trait : p_class->resolved_traits) {
			if (trait == nullptr || !trait->has_member(p_name)) {
				continue;
			}
			r_found_member = true;
			const GDScriptParser::ClassNode::Member &member = trait->get_member(p_name);
			if (member.type == GDScriptParser::ClassNode::Member::FUNCTION && member.function != nullptr && !member.function->type_parameters.is_empty()) {
				return member.function;
			}
			return nullptr;
		}
	}
	return nullptr;
}

GDScriptParser::DataType GDScriptAnalyzer::substitute_member_type(const GDScriptParser::DataType &p_member_type, const GDScriptParser::DataType &p_base, const GDScriptParser::FunctionNode *p_shadowing_method) {
	if (!p_base.has_type_arguments() || p_base.class_type == nullptr) {
		return p_member_type;
	}

	const Vector<GDScriptParser::TypeParameterNode *> &type_parameters = p_base.class_type->type_parameters;
	HashMap<StringName, GDScriptParser::DataType> bindings;
	const int binding_count = MIN(type_parameters.size(), p_base.type_arguments.size());
	for (int i = 0; i < binding_count; i++) {
		const GDScriptParser::TypeParameterNode *parameter = type_parameters[i];
		if (parameter != nullptr && parameter->identifier != nullptr) {
			bindings.insert(parameter->identifier->name, p_base.type_arguments[i]);
		}
	}
	// A method's own type parameters shadow same-named class parameters within its signature, so the
	// class specialization must not rewrite them (e.g. `func echo[T](v: T)` on a `Box[int]`).
	if (p_shadowing_method != nullptr) {
		for (const GDScriptParser::TypeParameterNode *parameter : p_shadowing_method->type_parameters) {
			if (parameter != nullptr && parameter->identifier != nullptr) {
				bindings.erase(parameter->identifier->name);
			}
		}
	}
	if (bindings.is_empty()) {
		return p_member_type;
	}
	return GDScriptParser::DataType::substitute(p_member_type, bindings);
}

bool GDScriptAnalyzer::apply_class_type_arguments(GDScriptParser::DataType &r_type, const Vector<GDScriptParser::TypeNode *> &p_argument_nodes, const GDScriptParser::Node *p_source, bool p_check_bounds, Vector<bool> *r_argument_failed) {
	const int expected_argument_count = r_type.class_type->type_parameters.size();
	if (expected_argument_count == 0) {
		push_error(vformat(R"(Class "%s" is not generic and cannot take type arguments.)", r_type.to_string()), p_source);
		return false;
	}
	if (p_argument_nodes.size() != expected_argument_count) {
		push_error(vformat(R"(Generic class "%s" expects %d type argument(s), but %d were given.)", r_type.to_string(), expected_argument_count, p_argument_nodes.size()), p_source);
		return false;
	}

	// Resolve every argument first so that bound checking can substitute the concrete argument
	// for any sibling parameter, regardless of declaration order. Arguments that fail to resolve
	// already reported an error and are excluded from bound checking to avoid double diagnostics.
	Vector<GDScriptParser::DataType> resolved_arguments;
	Vector<bool> argument_failed;
	Vector<const GDScriptParser::Node *> argument_sources;
	for (int i = 0; i < p_argument_nodes.size(); i++) {
		const int errors_before = parser->get_errors().size();
		resolved_arguments.push_back(type_from_metatype(resolve_datatype(p_argument_nodes[i])));
		argument_failed.push_back(parser->get_errors().size() > errors_before);
		argument_sources.push_back(p_argument_nodes[i]);
	}

	if (r_argument_failed != nullptr) {
		*r_argument_failed = argument_failed;
	}

	return bind_class_type_arguments(r_type, resolved_arguments, argument_failed, argument_sources, p_source, p_check_bounds);
}

bool GDScriptAnalyzer::bind_class_type_arguments(GDScriptParser::DataType &r_type, const Vector<GDScriptParser::DataType> &p_arguments, const Vector<bool> &p_argument_failed, const Vector<const GDScriptParser::Node *> &p_argument_sources, const GDScriptParser::Node *p_source, bool p_check_bounds) {
	r_type.type_arguments = p_arguments;
	if (!p_check_bounds) {
		// The caller will validate the bounds later (e.g. after a class's specialized base is fully
		// installed, so a self-referential argument is checked against the real chain).
		return true;
	}
	return check_class_type_argument_bounds(r_type, p_argument_failed, p_argument_sources);
}

bool GDScriptAnalyzer::check_class_type_argument_bounds(GDScriptParser::DataType &r_type, const Vector<bool> &p_argument_failed, const Vector<const GDScriptParser::Node *> &p_argument_sources) {
	const Vector<GDScriptParser::TypeParameterNode *> &type_parameters = r_type.class_type->type_parameters;

	// Bind every parameter to its argument so a dependent bound like `[U: Resource, T: U]` (or its
	// forward-referencing form `[T: U, U: Resource]`) is checked against the concrete argument
	// supplied for the referenced sibling.
	HashMap<StringName, GDScriptParser::DataType> bindings;
	for (int i = 0; i < r_type.type_arguments.size(); i++) {
		const GDScriptParser::TypeParameterNode *parameter = type_parameters[i];
		if (parameter != nullptr && parameter->identifier != nullptr) {
			bindings.insert(parameter->identifier->name, r_type.type_arguments[i]);
		}
	}

	bool bound_violation = false;
	for (int i = 0; i < r_type.type_arguments.size(); i++) {
		const GDScriptParser::TypeParameterNode *parameter = type_parameters[i];
		// An argument that failed to resolve already reported an error; skip it to avoid a second
		// diagnostic (a failed resolve yields a `Variant` placeholder that would otherwise be checked
		// against the bound and re-rejected).
		if (parameter == nullptr || parameter->bound == nullptr || p_argument_failed[i]) {
			continue;
		}
		// Resolve the bound in the generic class's own scope so relative bound names bind to the
		// declaring class rather than the (possibly unrelated) use site, where an enclosing
		// class or method type parameter could otherwise shadow them.
		GDScriptParser::ClassNode *previous_class = parser->current_class;
		GDScriptParser::FunctionNode *previous_function = parser->current_function;
		parser->current_class = r_type.class_type;
		parser->current_function = nullptr;
		const GDScriptParser::DataType bound = type_from_metatype(resolve_datatype(parameter->bound));
		parser->current_class = previous_class;
		parser->current_function = previous_function;

		// An unresolved or unconstrained (`Variant`) bound imposes no requirement.
		if (!bound.is_set() || bound.is_variant()) {
			continue;
		}
		const GDScriptParser::DataType effective_bound = bindings.is_empty() ? bound : GDScriptParser::DataType::substitute(bound, bindings);
		if (!type_argument_satisfies_bound(r_type.type_arguments[i], effective_bound)) {
			push_error(vformat(R"(Type argument "%s" does not satisfy the bound "%s" of type parameter "%s".)", r_type.type_arguments[i].to_string(), effective_bound.to_string(), parameter->identifier->name), p_argument_sources[i]);
			bound_violation = true;
		}
	}
	return !bound_violation;
}

GDScriptParser::DataType GDScriptAnalyzer::specialize_ancestor_type(const GDScriptParser::DataType &p_base, const GDScriptParser::ClassNode *p_target) {
	// Walks the inheritance chain from a specialized base down to `p_target`, applying each level's
	// type arguments so a member declared in an ancestor sees the concrete arguments supplied at the
	// most-derived use site. For `Stack[int] extends List[U]`, reaching `List` yields `List[int]`.
	GDScriptParser::DataType current = p_base;
	while (current.class_type != nullptr) {
		if (current.class_type == p_target) {
			return current;
		}

		GDScriptParser::DataType parent = current.class_type->base_type;
		if (parent.class_type == nullptr) {
			break;
		}

		// Rewrite the parent handle's type arguments (which reference `current`'s parameters) into
		// the concrete arguments bound at this level.
		if (current.has_type_arguments()) {
			const Vector<GDScriptParser::TypeParameterNode *> &type_parameters = current.class_type->type_parameters;
			HashMap<StringName, GDScriptParser::DataType> bindings;
			const int binding_count = MIN(type_parameters.size(), current.type_arguments.size());
			for (int i = 0; i < binding_count; i++) {
				const GDScriptParser::TypeParameterNode *parameter = type_parameters[i];
				if (parameter != nullptr && parameter->identifier != nullptr) {
					bindings.insert(parameter->identifier->name, current.type_arguments[i]);
				}
			}
			if (!bindings.is_empty()) {
				parent = GDScriptParser::DataType::substitute(parent, bindings);
			}
		}

		current = parent;
	}

	// `p_target` is not on the inheritance chain (e.g. an outer class or applied trait); return a
	// non-specialized handle so member substitution is a no-op.
	GDScriptParser::DataType fallback = p_base;
	fallback.type_arguments.clear();
	return fallback;
}

void GDScriptAnalyzer::resolve_class_member(GDScriptParser::ClassNode *p_class, const StringName &p_name, const GDScriptParser::Node *p_source) {
	ERR_FAIL_COND(!p_class->has_member(p_name));
	resolve_class_member(p_class, p_class->members_indices[p_name], p_source);
}

void GDScriptAnalyzer::resolve_class_member(GDScriptParser::ClassNode *p_class, int p_index, const GDScriptParser::Node *p_source) {
	ERR_FAIL_INDEX(p_index, p_class->members.size());

	GDScriptParser::ClassNode::Member &member = p_class->members.write[p_index];
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = member.get_source_node();
	}

	Ref<GDScriptParserRef> parser_ref = ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class member", p_source);
	Finally finally([&]() {
		ensure_cached_external_parser_for_class(member.get_datatype().class_type, p_class, "Trying to resolve datatype of class member", p_source);
		GDScriptParser::DataType member_type = member.get_datatype();
		for (int i = 0; i < member_type.get_container_element_type_count(); ++i) {
			ensure_cached_external_parser_for_class(member_type.get_container_element_type(i).class_type, p_class, "Trying to resolve datatype of class member", p_source);
		}
	});

	if (member.get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve member "%s": Cyclic reference.)", member.get_name()), p_source);
		return;
	}

	if (member.get_datatype().is_set()) {
		return;
	}

	// If it's already resolving, that's ok.
	if (!p_class->base_type.is_resolving()) {
		Error err = resolve_class_inheritance(p_class);
		if (err) {
			return;
		}
	}

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			// Error already pushed.
			return;
		}

		Error err = parser_ref->raise_status(GDScriptParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s (While resolving external class member "%s").)", p_class->get_datatype().script_path, error_names[err], member.get_name()), p_source);
			return;
		}

		GDScriptAnalyzer *other_analyzer = parser_ref->get_analyzer();
		GDScriptParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_member(p_class, p_index);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve external class member "%s".)", member.get_name()), p_source);
			return;
		}

		return;
	}

	GDScriptParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	GDScriptParser::DataType resolving_datatype;
	resolving_datatype.kind = GDScriptParser::DataType::RESOLVING;

	{
#ifdef DEBUG_ENABLED
		GDScriptParser::Node *member_node = member.get_source_node();
		if (member_node && member_node->type != GDScriptParser::Node::ANNOTATION) {
			// Apply @warning_ignore annotations before resolving member.
			for (GDScriptParser::AnnotationNode *&E : member_node->annotations) {
				if (E->name == SNAME("@warning_ignore")) {
					resolve_annotation(E);
					E->apply(parser, member.variable, p_class);
				}
			}
		}
#endif // DEBUG_ENABLED
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				bool previous_static_context = static_context;
				static_context = member.variable->is_static;

				check_class_member_name_conflict(p_class, member.variable->identifier->name, member.variable);

				member.variable->set_datatype(resolving_datatype);
				resolve_variable(member.variable, false);
				resolve_pending_lambda_bodies();

				// Apply annotations.
				for (GDScriptParser::AnnotationNode *&E : member.variable->annotations) {
					if (E->name != SNAME("@warning_ignore")) {
						resolve_annotation(E);
						E->apply(parser, member.variable, p_class);
					}
				}

				static_context = previous_static_context;

#ifdef DEBUG_ENABLED
				if (member.variable->exported && member.variable->onready) {
					parser->push_warning(member.variable, GDScriptWarning::ONREADY_WITH_EXPORT);
				}
				if (member.variable->initializer) {
					// Check if it is call to get_node() on self (using shorthand $ or not), so we can check if @onready is needed.
					// This could be improved by traversing the expression fully and checking the presence of get_node at any level.
					if (!member.variable->is_static && !member.variable->onready && member.variable->initializer && (member.variable->initializer->type == GDScriptParser::Node::GET_NODE || member.variable->initializer->type == GDScriptParser::Node::CALL || member.variable->initializer->type == GDScriptParser::Node::CAST)) {
						GDScriptParser::Node *expr = member.variable->initializer;
						if (expr->type == GDScriptParser::Node::CAST) {
							expr = static_cast<GDScriptParser::CastNode *>(expr)->operand;
						}
						bool is_get_node = expr->type == GDScriptParser::Node::GET_NODE;
						bool is_using_shorthand = is_get_node;
						if (!is_get_node && expr->type == GDScriptParser::Node::CALL) {
							is_using_shorthand = false;
							GDScriptParser::CallNode *call = static_cast<GDScriptParser::CallNode *>(expr);
							if (call->function_name == SNAME("get_node")) {
								switch (call->get_callee_type()) {
									case GDScriptParser::Node::IDENTIFIER: {
										is_get_node = true;
									} break;
									case GDScriptParser::Node::SUBSCRIPT: {
										GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(call->callee);
										is_get_node = subscript->is_attribute && subscript->base->type == GDScriptParser::Node::SELF;
									} break;
									default:
										break;
								}
							}
						}
						if (is_get_node) {
							String offending_syntax = "get_node()";
							if (is_using_shorthand) {
								GDScriptParser::GetNodeNode *get_node_node = static_cast<GDScriptParser::GetNodeNode *>(expr);
								offending_syntax = get_node_node->use_dollar ? "$" : "%";
							}
							parser->push_warning(member.variable, GDScriptWarning::GET_NODE_DEFAULT_WITHOUT_ONREADY, offending_syntax);
						}
					}
				}
#endif // DEBUG_ENABLED
			} break;
			case GDScriptParser::ClassNode::Member::CONSTANT: {
				check_class_member_name_conflict(p_class, member.constant->identifier->name, member.constant);
				member.constant->set_datatype(resolving_datatype);
				resolve_constant(member.constant, false);

				// Apply annotations.
				for (GDScriptParser::AnnotationNode *&E : member.constant->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.constant, p_class);
				}
			} break;
			case GDScriptParser::ClassNode::Member::SIGNAL: {
				check_class_member_name_conflict(p_class, member.signal->identifier->name, member.signal);

				member.signal->set_datatype(resolving_datatype);

				// This is the _only_ way to declare a signal. Therefore, we can generate its
				// MethodInfo inline so it's a tiny bit more efficient.
				MethodInfo mi = MethodInfo(member.signal->identifier->name);

				for (int j = 0; j < member.signal->parameters.size(); j++) {
					GDScriptParser::ParameterNode *param = member.signal->parameters[j];
					GDScriptParser::DataType param_type = type_from_metatype(resolve_datatype(param->datatype_specifier));
					param->set_datatype(param_type);
#ifdef DEBUG_ENABLED
					if (param->datatype_specifier == nullptr) {
						parser->push_warning(param, GDScriptWarning::UNTYPED_DECLARATION, "Parameter", param->identifier->name);
					}
#endif // DEBUG_ENABLED
					mi.arguments.push_back(param_type.to_property_info(param->identifier->name));
					// Signals do not support parameter default values.
				}
				member.signal->set_datatype(make_signal_type(mi, member.signal));
				member.signal->method_info = mi;

				// Apply annotations.
				for (GDScriptParser::AnnotationNode *&E : member.signal->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.signal, p_class);
				}
			} break;
			case GDScriptParser::ClassNode::Member::ENUM: {
				check_class_member_name_conflict(p_class, member.m_enum->identifier->name, member.m_enum);

				member.m_enum->set_datatype(resolving_datatype);
				GDScriptParser::DataType enum_type = make_class_enum_type(member.m_enum->identifier->name, p_class, parser->script_path, true);

				const GDScriptParser::EnumNode *prev_enum = current_enum;
				current_enum = member.m_enum;

				Dictionary dictionary;
				for (int j = 0; j < member.m_enum->values.size(); j++) {
					GDScriptParser::EnumNode::Value &element = member.m_enum->values.write[j];

					if (element.custom_value) {
						reduce_expression(element.custom_value);
						if (!element.custom_value->is_constant) {
							push_error(R"(Enum values must be constant.)", element.custom_value);
						} else if (element.custom_value->reduced_value.get_type() != Variant::INT) {
							push_error(R"(Enum values must be integers.)", element.custom_value);
						} else {
							element.value = element.custom_value->reduced_value;
							element.resolved = true;
						}
					} else {
						if (element.index > 0) {
							element.value = element.parent_enum->values[element.index - 1].value + 1;
						} else {
							element.value = 0;
						}
						element.resolved = true;
					}

					enum_type.enum_values[element.identifier->name] = element.value;
					dictionary[String(element.identifier->name)] = element.value;

#ifdef DEBUG_ENABLED
					// Named enum identifiers do not shadow anything since you can only access them with `NamedEnum.ENUM_VALUE`.
					if (member.m_enum->identifier->name == StringName()) {
						is_shadowing(element.identifier, "enum member", false);
					}
#endif // DEBUG_ENABLED
				}

				current_enum = prev_enum;

				dictionary.make_read_only();
				member.m_enum->set_datatype(enum_type);
				member.m_enum->dictionary = dictionary;

				// Apply annotations.
				for (GDScriptParser::AnnotationNode *&E : member.m_enum->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.m_enum, p_class);
				}
			} break;
			case GDScriptParser::ClassNode::Member::FUNCTION:
				for (GDScriptParser::AnnotationNode *&E : member.function->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.function, p_class);
				}
				resolve_function_signature(member.function, p_source);
				break;
			case GDScriptParser::ClassNode::Member::ENUM_VALUE: {
				member.enum_value.identifier->set_datatype(resolving_datatype);

				if (member.enum_value.custom_value) {
					check_class_member_name_conflict(p_class, member.enum_value.identifier->name, member.enum_value.custom_value);

					const GDScriptParser::EnumNode *prev_enum = current_enum;
					current_enum = member.enum_value.parent_enum;
					reduce_expression(member.enum_value.custom_value);
					current_enum = prev_enum;

					if (!member.enum_value.custom_value->is_constant) {
						push_error(R"(Enum values must be constant.)", member.enum_value.custom_value);
					} else if (member.enum_value.custom_value->reduced_value.get_type() != Variant::INT) {
						push_error(R"(Enum values must be integers.)", member.enum_value.custom_value);
					} else {
						member.enum_value.value = member.enum_value.custom_value->reduced_value;
						member.enum_value.resolved = true;
					}
				} else {
					check_class_member_name_conflict(p_class, member.enum_value.identifier->name, member.enum_value.parent_enum);

					if (member.enum_value.index > 0) {
						const GDScriptParser::EnumNode::Value &prev_value = member.enum_value.parent_enum->values[member.enum_value.index - 1];
						resolve_class_member(p_class, prev_value.identifier->name, member.enum_value.identifier);
						member.enum_value.value = prev_value.value + 1;
					} else {
						member.enum_value.value = 0;
					}
					member.enum_value.resolved = true;
				}

				// Also update the original references.
				member.enum_value.parent_enum->values.set(member.enum_value.index, member.enum_value);

				member.enum_value.identifier->set_datatype(make_class_enum_type(UNNAMED_ENUM, p_class, parser->script_path, false));
			} break;
			case GDScriptParser::ClassNode::Member::CLASS:
				check_class_member_name_conflict(p_class, member.m_class->identifier->name, member.m_class);
				// If it's already resolving, that's ok.
				if (!member.m_class->base_type.is_resolving()) {
					resolve_class_inheritance(member.m_class, p_source);
				}
				break;
			case GDScriptParser::ClassNode::Member::GROUP:
				// No-op, but needed to silence warnings.
				break;
			case GDScriptParser::ClassNode::Member::UNDEFINED:
				ERR_PRINT("Trying to resolve undefined member.");
				break;
		}
	}

	parser->current_class = previous_class;
}

void GDScriptAnalyzer::resolve_class_interface(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<GDScriptParserRef> parser_ref = ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class interface", p_source);

	if (!p_class->resolved_interface) {
#ifdef DEBUG_ENABLED
		bool has_static_data = p_class->has_static_data;
#endif // DEBUG_ENABLED

		if (!parser->has_class(p_class)) {
			if (parser_ref.is_null()) {
				// Error already pushed.
				return;
			}

			Error err = parser_ref->raise_status(GDScriptParserRef::PARSED);
			if (err) {
				push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
				return;
			}

			GDScriptAnalyzer *other_analyzer = parser_ref->get_analyzer();
			GDScriptParser *other_parser = parser_ref->get_parser();

			int error_count = other_parser->errors.size();
			other_analyzer->resolve_class_interface(p_class);
			if (other_parser->errors.size() > error_count) {
				push_error(vformat(R"(Could not resolve class "%s".)", p_class->fqcn), p_source);
				return;
			}

			return;
		}

		p_class->resolved_interface = true;

		if (resolve_class_inheritance(p_class) != OK) {
			return;
		}
		if (resolve_trait_uses(p_class, p_source) != OK) {
			return;
		}

		// Resolve declared type-parameter bounds eagerly so runtime reflection can report them even
		// when a parameter is never referenced inside the class body. A class parameter's bound is
		// resolved in its declaring class scope, mirroring `resolve_type_parameter`.
		if (!p_class->type_parameters.is_empty()) {
			GDScriptParser::ClassNode *previous_class = parser->current_class;
			GDScriptParser::FunctionNode *previous_function = parser->current_function;
			parser->current_class = p_class;
			parser->current_function = nullptr;
			for (GDScriptParser::TypeParameterNode *parameter : p_class->type_parameters) {
				if (parameter != nullptr && parameter->bound != nullptr) {
					parameter->resolved_bound = type_from_metatype(resolve_datatype(parameter->bound));
				}
			}
			parser->current_class = previous_class;
			parser->current_function = previous_function;
		}

		GDScriptParser::DataType base_type = p_class->base_type;
		if (base_type.kind == GDScriptParser::DataType::CLASS) {
			GDScriptParser::ClassNode *base_class = base_type.class_type;
			resolve_class_interface(base_class, p_class);
		}

		for (int i = 0; i < p_class->members.size(); i++) {
			resolve_class_member(p_class, i);

#ifdef DEBUG_ENABLED
			if (!has_static_data) {
				GDScriptParser::ClassNode::Member member = p_class->members[i];
				if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
					has_static_data = member.m_class->has_static_data;
				}
			}
#endif // DEBUG_ENABLED
		}

#ifdef DEBUG_ENABLED
		if (!has_static_data && p_class->annotated_static_unload) {
			GDScriptParser::Node *static_unload = nullptr;
			for (GDScriptParser::AnnotationNode *node : p_class->annotations) {
				if (node->name == "@static_unload") {
					static_unload = node;
					break;
				}
			}
			parser->push_warning(static_unload ? static_unload : p_class, GDScriptWarning::REDUNDANT_STATIC_UNLOAD);
		}
#endif // DEBUG_ENABLED
	}
}

void GDScriptAnalyzer::resolve_class_interface(GDScriptParser::ClassNode *p_class, bool p_recursive) {
	resolve_class_interface(p_class);

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			GDScriptParser::ClassNode::Member member = p_class->members[i];
			if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
				resolve_class_interface(member.m_class, true);
			}
		}
	}
}

void GDScriptAnalyzer::resolve_class_body(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<GDScriptParserRef> parser_ref = ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class body", p_source);

	if (p_class->resolved_body) {
		return;
	}

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			// Error already pushed.
			return;
		}

		Error err = parser_ref->raise_status(GDScriptParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
			return;
		}

		GDScriptAnalyzer *other_analyzer = parser_ref->get_analyzer();
		GDScriptParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_body(p_class);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve class "%s".)", p_class->fqcn), p_source);
			return;
		}

		return;
	}

	p_class->resolved_body = true;

	GDScriptParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	resolve_class_interface(p_class, p_source);

	// A class flattens its applied traits' members — including method bodies — into
	// itself at compile time. Identifiers inside a trait body only get their source
	// resolved when that trait is fully solved, so external traits must be raised to
	// `FULLY_SOLVED` here before the implementer's own body (and later the compiler)
	// flattens them. Only the directly applied traits are raised: a trait reached
	// transitively is fully solved by the body resolution of the trait that applies it
	// (reachable from that trait's parser, not necessarily this one). Inline traits are
	// solved as members of this same parser.
	for (const GDScriptParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		GDScriptParser::ClassNode *trait = trait_use.resolved_trait;
		if (trait == nullptr) {
			continue;
		}
		Ref<GDScriptParserRef> trait_parser_ref = ensure_cached_external_parser_for_class(trait, p_class, "Trying to resolve trait body for flattening", p_source);
		if (trait_parser_ref.is_valid()) {
			Error err = trait_parser_ref->raise_status(GDScriptParserRef::FULLY_SOLVED);
			if (err != OK) {
				push_error(vformat(R"(Could not resolve body of trait "%s" applied by "%s".)", _class_or_trait_name(trait), _class_or_trait_name(p_class)), p_source);
			}
		}
	}

	GDScriptParser::DataType base_type = p_class->base_type;
	if (base_type.kind == GDScriptParser::DataType::CLASS) {
		GDScriptParser::ClassNode *base_class = base_type.class_type;
		resolve_class_body(base_class, p_class);
	}

	// Do functions, properties, and groups now.
	for (int i = 0; i < p_class->members.size(); i++) {
		GDScriptParser::ClassNode::Member member = p_class->members[i];
		if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
			// Apply annotations.
			for (GDScriptParser::AnnotationNode *&E : member.function->annotations) {
				resolve_annotation(E);
				E->apply(parser, member.function, p_class);
			}
			resolve_function_body(member.function);
		} else if (member.type == GDScriptParser::ClassNode::Member::VARIABLE && member.variable->property != GDScriptParser::VariableNode::PROP_NONE) {
			if (member.variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					member.variable->getter->return_type = member.variable->datatype_specifier;
					member.variable->getter->set_datatype(member.get_datatype());

					resolve_function_body(member.variable->getter);
				}
				if (member.variable->setter != nullptr) {
					ERR_CONTINUE(member.variable->setter->parameters.is_empty());
					member.variable->setter->parameters[0]->datatype_specifier = member.variable->datatype_specifier;
					member.variable->setter->parameters[0]->set_datatype(member.get_datatype());

					resolve_function_body(member.variable->setter);
				}
			}
		} else if (member.type == GDScriptParser::ClassNode::Member::GROUP) {
			// Apply annotation (`@export_{category,group,subgroup}`).
			resolve_annotation(member.annotation);
			member.annotation->apply(parser, nullptr, p_class);
		}
	}

	// Check unused variables and datatypes of property getters and setters.
	for (int i = 0; i < p_class->members.size(); i++) {
		GDScriptParser::ClassNode::Member member = p_class->members[i];
		if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
#ifdef DEBUG_ENABLED
			if (member.variable->usages == 0 && String(member.variable->identifier->name).begins_with("_")) {
				parser->push_warning(member.variable->identifier, GDScriptWarning::UNUSED_PRIVATE_CLASS_VARIABLE, member.variable->identifier->name);
			}
#endif // DEBUG_ENABLED

			if (member.variable->property == GDScriptParser::VariableNode::PROP_SETGET) {
				GDScriptParser::FunctionNode *getter_function = nullptr;
				GDScriptParser::FunctionNode *setter_function = nullptr;

				bool has_valid_getter = false;
				bool has_valid_setter = false;

				if (member.variable->getter_pointer != nullptr) {
					if (p_class->has_function(member.variable->getter_pointer->name)) {
						getter_function = p_class->get_member(member.variable->getter_pointer->name).function;
					}

					if (getter_function == nullptr) {
						push_error(vformat(R"(Getter "%s" not found.)", member.variable->getter_pointer->name), member.variable);
					} else {
						GDScriptParser::DataType return_datatype = getter_function->datatype;
						if (getter_function->return_type != nullptr) {
							return_datatype = getter_function->return_type->datatype;
							return_datatype.is_meta_type = false;
						}

						if (getter_function->parameters.size() != 0 || return_datatype.has_no_type()) {
							push_error(vformat(R"(Function "%s" cannot be used as getter because of its signature.)", getter_function->identifier->name), member.variable);
						} else if (!is_type_compatible(member.variable->datatype, return_datatype, true)) {
							push_error(vformat(R"(Function with return type "%s" cannot be used as getter for a property of type "%s".)", return_datatype.to_string(), member.variable->datatype.to_string()), member.variable);

						} else {
							has_valid_getter = true;
#ifdef DEBUG_ENABLED
							if (member.variable->datatype.builtin_type == Variant::INT && return_datatype.builtin_type == Variant::FLOAT) {
								parser->push_warning(member.variable, GDScriptWarning::NARROWING_CONVERSION);
							}
#endif // DEBUG_ENABLED
						}
					}
				}

				if (member.variable->setter_pointer != nullptr) {
					if (p_class->has_function(member.variable->setter_pointer->name)) {
						setter_function = p_class->get_member(member.variable->setter_pointer->name).function;
					}

					if (setter_function == nullptr) {
						push_error(vformat(R"(Setter "%s" not found.)", member.variable->setter_pointer->name), member.variable);

					} else if (setter_function->parameters.size() != 1) {
						push_error(vformat(R"(Function "%s" cannot be used as setter because of its signature.)", setter_function->identifier->name), member.variable);

					} else if (!is_type_compatible(member.variable->datatype, setter_function->parameters[0]->datatype, true)) {
						push_error(vformat(R"(Function with argument type "%s" cannot be used as setter for a property of type "%s".)", setter_function->parameters[0]->datatype.to_string(), member.variable->datatype.to_string()), member.variable);

					} else {
						has_valid_setter = true;

#ifdef DEBUG_ENABLED
						if (member.variable->datatype.builtin_type == Variant::FLOAT && setter_function->parameters[0]->datatype.builtin_type == Variant::INT) {
							parser->push_warning(member.variable, GDScriptWarning::NARROWING_CONVERSION);
						}
#endif // DEBUG_ENABLED
					}
				}

				if (member.variable->datatype.is_variant() && has_valid_getter && has_valid_setter) {
					if (!is_type_compatible(getter_function->datatype, setter_function->parameters[0]->datatype, true)) {
						push_error(vformat(R"(Getter with type "%s" cannot be used along with setter of type "%s".)", getter_function->datatype.to_string(), setter_function->parameters[0]->datatype.to_string()), member.variable);
					}
				}
			}
		} else if (member.type == GDScriptParser::ClassNode::Member::SIGNAL) {
#ifdef DEBUG_ENABLED
			if (member.signal->usages == 0) {
				parser->push_warning(member.signal->identifier, GDScriptWarning::UNUSED_SIGNAL, member.signal->identifier->name);
			}
#endif // DEBUG_ENABLED
		}
	}

	if (!pending_body_resolution_lambdas.is_empty()) {
		ERR_PRINT("GDScript bug (please report): Not all pending lambda bodies were resolved in time.");
		resolve_pending_lambda_bodies();
	}

	// Resolve base abstract class/method implementation requirements.
	if (!p_class->is_abstract && !p_class->is_trait) {
		HashSet<StringName> implemented_funcs;
		const GDScriptParser::ClassNode *base_class = p_class;
		while (base_class != nullptr) {
			if (!base_class->is_abstract && base_class != p_class) {
				break;
			}
			for (GDScriptParser::ClassNode::Member member : base_class->members) {
				if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
					if (member.function->is_abstract) {
						if (base_class == p_class) {
							const String class_name = p_class->identifier == nullptr ? p_class->fqcn.get_file() : String(p_class->identifier->name);
							push_error(vformat(R"*(Class "%s" is not abstract but contains abstract methods. Mark the class as "@abstract" or remove "@abstract" from all methods in this class.)*", class_name), p_class);
							break;
						} else if (!implemented_funcs.has(member.function->identifier->name)) {
							const String class_name = p_class->identifier == nullptr ? p_class->fqcn.get_file() : String(p_class->identifier->name);
							const String base_class_name = base_class->identifier == nullptr ? base_class->fqcn.get_file() : String(base_class->identifier->name);
							push_error(vformat(R"*(Class "%s" must implement "%s.%s()" and other inherited abstract methods or be marked as "@abstract".)*", class_name, base_class_name, member.function->identifier->name), p_class);
							break;
						}
					} else {
						implemented_funcs.insert(member.function->identifier->name);
					}
				}
			}
			if (base_class->base_type.kind == GDScriptParser::DataType::CLASS) {
				base_class = base_class->base_type.class_type;
			} else if (base_class->base_type.kind == GDScriptParser::DataType::SCRIPT) {
				Ref<GDScriptParserRef> base_parser_ref = parser->get_depended_parser_for(base_class->base_type.script_path);
				ERR_BREAK(base_parser_ref.is_null());
				base_class = base_parser_ref->get_parser()->head;
			} else {
				break;
			}
		}
	}

	validate_trait_conflicts(p_class);
	validate_trait_requirements(p_class);

	parser->current_class = previous_class;
}

void GDScriptAnalyzer::resolve_class_body(GDScriptParser::ClassNode *p_class, bool p_recursive) {
	resolve_class_body(p_class);

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			GDScriptParser::ClassNode::Member member = p_class->members[i];
			if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
				resolve_class_body(member.m_class, true);
			}
		}
	}
}

void GDScriptAnalyzer::resolve_node(GDScriptParser::Node *p_node, bool p_is_root) {
	ERR_FAIL_NULL_MSG(p_node, "Trying to resolve type of a null node.");

	switch (p_node->type) {
		case GDScriptParser::Node::NONE:
			break; // Unreachable.
		case GDScriptParser::Node::CLASS:
			// NOTE: Currently this route is never executed, `resolve_class_*()` is called directly.
			if (OK == resolve_class_inheritance(static_cast<GDScriptParser::ClassNode *>(p_node), true)) {
				resolve_class_interface(static_cast<GDScriptParser::ClassNode *>(p_node), true);
				resolve_class_body(static_cast<GDScriptParser::ClassNode *>(p_node), true);
			}
			break;
		case GDScriptParser::Node::CONSTANT:
			resolve_constant(static_cast<GDScriptParser::ConstantNode *>(p_node), true);
			break;
		case GDScriptParser::Node::FOR:
			resolve_for(static_cast<GDScriptParser::ForNode *>(p_node));
			break;
		case GDScriptParser::Node::IF:
			resolve_if(static_cast<GDScriptParser::IfNode *>(p_node));
			break;
		case GDScriptParser::Node::SUITE:
			resolve_suite(static_cast<GDScriptParser::SuiteNode *>(p_node));
			break;
		case GDScriptParser::Node::VARIABLE:
			resolve_variable(static_cast<GDScriptParser::VariableNode *>(p_node), true);
			break;
		case GDScriptParser::Node::WHILE:
			resolve_while(static_cast<GDScriptParser::WhileNode *>(p_node));
			break;
		case GDScriptParser::Node::ANNOTATION:
			resolve_annotation(static_cast<GDScriptParser::AnnotationNode *>(p_node));
			break;
		case GDScriptParser::Node::ASSERT:
			resolve_assert(static_cast<GDScriptParser::AssertNode *>(p_node));
			break;
		case GDScriptParser::Node::MATCH:
			resolve_match(static_cast<GDScriptParser::MatchNode *>(p_node));
			break;
		case GDScriptParser::Node::MATCH_BRANCH:
			resolve_match_branch(static_cast<GDScriptParser::MatchBranchNode *>(p_node), nullptr);
			break;
		case GDScriptParser::Node::PARAMETER:
			resolve_parameter(static_cast<GDScriptParser::ParameterNode *>(p_node));
			break;
		case GDScriptParser::Node::PATTERN:
			resolve_match_pattern(static_cast<GDScriptParser::PatternNode *>(p_node), nullptr);
			break;
		case GDScriptParser::Node::RETURN:
			resolve_return(static_cast<GDScriptParser::ReturnNode *>(p_node));
			break;
		case GDScriptParser::Node::TYPE:
			resolve_datatype(static_cast<GDScriptParser::TypeNode *>(p_node));
			break;
		// Resolving expression is the same as reducing them.
		case GDScriptParser::Node::ARRAY:
		case GDScriptParser::Node::ASSIGNMENT:
		case GDScriptParser::Node::AWAIT:
		case GDScriptParser::Node::BINARY_OPERATOR:
		case GDScriptParser::Node::CALL:
		case GDScriptParser::Node::CAST:
		case GDScriptParser::Node::DICTIONARY:
		case GDScriptParser::Node::GET_NODE:
		case GDScriptParser::Node::IDENTIFIER:
		case GDScriptParser::Node::LAMBDA:
		case GDScriptParser::Node::LITERAL:
		case GDScriptParser::Node::PRELOAD:
		case GDScriptParser::Node::SELF:
		case GDScriptParser::Node::SUBSCRIPT:
		case GDScriptParser::Node::TERNARY_OPERATOR:
		case GDScriptParser::Node::TYPE_TEST:
		case GDScriptParser::Node::UNARY_OPERATOR:
			reduce_expression(static_cast<GDScriptParser::ExpressionNode *>(p_node), p_is_root);
			break;
		case GDScriptParser::Node::ANNOTATION_DECLARATION:
		case GDScriptParser::Node::BREAK:
		case GDScriptParser::Node::BREAKPOINT:
		case GDScriptParser::Node::CONTINUE:
		case GDScriptParser::Node::ENUM:
		case GDScriptParser::Node::FUNCTION:
		case GDScriptParser::Node::PASS:
		case GDScriptParser::Node::SIGNAL:
		case GDScriptParser::Node::TYPE_PARAMETER:
			// Nothing to do. Custom annotation declarations are resolved in a later pass.
			break;
	}
}

void GDScriptAnalyzer::resolve_annotation(GDScriptParser::AnnotationNode *p_annotation) {
	if (p_annotation->is_custom) {
		// Unresolved custom annotation usage. Import-aware resolution and validation of custom
		// annotations land in a later analyzer change; until then the usage is preserved but
		// neither applied as a built-in nor reported here.
		return;
	}

	ERR_FAIL_COND_MSG(!parser->valid_annotations.has(p_annotation->name), vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	const MethodInfo &annotation_info = parser->valid_annotations[p_annotation->name].info;

	for (int64_t i = 0, j = 0; i < p_annotation->arguments.size(); i++) {
		GDScriptParser::ExpressionNode *argument = p_annotation->arguments[i];
		const PropertyInfo &argument_info = annotation_info.arguments[j];

		if (j + 1 < annotation_info.arguments.size()) {
			++j;
		}

		reduce_expression(argument);

		if (!argument->is_constant) {
			push_error(vformat(R"(Argument %d of annotation "%s" isn't a constant expression.)", i + 1, p_annotation->name), argument);
			return;
		}

		Variant value = argument->reduced_value;

		if (value.get_type() != argument_info.type) {
#ifdef DEBUG_ENABLED
			if (argument_info.type == Variant::INT && value.get_type() == Variant::FLOAT) {
				parser->push_warning(argument, GDScriptWarning::NARROWING_CONVERSION);
			}
#endif // DEBUG_ENABLED

			if (!Variant::can_convert_strict(value.get_type(), argument_info.type)) {
				push_error(vformat(R"(Invalid argument for annotation "%s": argument %d should be "%s" but is "%s".)", p_annotation->name, i + 1, Variant::get_type_name(argument_info.type), argument->get_datatype().to_string()), argument);
				return;
			}

			Variant converted_to;
			const Variant *converted_from = &value;
			Callable::CallError call_error;
			Variant::construct(argument_info.type, converted_to, &converted_from, 1, call_error);

			if (call_error.error != Callable::CallError::CALL_OK) {
				push_error(vformat(R"(Cannot convert argument %d of annotation "%s" from "%s" to "%s".)", i + 1, p_annotation->name, Variant::get_type_name(value.get_type()), Variant::get_type_name(argument_info.type)), argument);
				return;
			}

			value = converted_to;
		}

		p_annotation->resolved_arguments.push_back(value);
	}
}

void GDScriptAnalyzer::resolve_function_signature(GDScriptParser::FunctionNode *p_function, const GDScriptParser::Node *p_source, bool p_is_lambda) {
	if (p_source == nullptr) {
		p_source = p_function;
	}

	StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();

	if (p_function->get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve function "%s": Cyclic reference.)", function_name), p_source);
		return;
	}

	if (p_function->resolved_signature) {
		return;
	}
	p_function->resolved_signature = true;

	GDScriptParser::FunctionNode *previous_function = parser->current_function;
	parser->current_function = p_function;
	bool previous_static_context = static_context;
	if (p_is_lambda) {
		// For lambdas this is determined from the context, the `static` keyword is not allowed.
		p_function->is_static = static_context;
	} else {
		// For normal functions, this is determined in the parser by the `static` keyword.
		static_context = p_function->is_static;
	}

	// Resolve type-parameter bounds as part of the signature (in the function's own scope) so a call
	// site can enforce a generic-method bound even when the bounded parameter appears only in the
	// body, whose resolution may not have run yet.
	for (GDScriptParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
		if (type_parameter != nullptr && type_parameter->bound != nullptr) {
			resolve_datatype(type_parameter->bound);
		}
	}

	MethodInfo method_info;
	method_info.name = function_name;
	if (p_function->is_static) {
		method_info.flags |= MethodFlags::METHOD_FLAG_STATIC;
	}
	if (p_function->is_coroutine) {
		method_info.flags |= MethodFlags::METHOD_FLAG_ASYNC;
	}

	GDScriptParser::DataType prev_datatype = p_function->get_datatype();

	GDScriptParser::DataType resolving_datatype;
	resolving_datatype.kind = GDScriptParser::DataType::RESOLVING;
	p_function->set_datatype(resolving_datatype);

#ifdef TOOLS_ENABLED
	int default_value_count = 0;
#endif // TOOLS_ENABLED

#ifdef DEBUG_ENABLED
	String function_visible_name = function_name;
	if (function_name == StringName()) {
		function_visible_name = p_is_lambda ? "<anonymous lambda>" : "<unknown function>";
	}
#endif // DEBUG_ENABLED

	for (int i = 0; i < p_function->parameters.size(); i++) {
		resolve_parameter(p_function->parameters[i]);
		method_info.arguments.push_back(p_function->parameters[i]->get_datatype().to_property_info(p_function->parameters[i]->identifier->name));
#ifdef DEBUG_ENABLED
		if (p_function->parameters[i]->usages == 0 && !String(p_function->parameters[i]->identifier->name).begins_with("_") && !p_function->is_abstract) {
			parser->push_warning(p_function->parameters[i]->identifier, GDScriptWarning::UNUSED_PARAMETER, function_visible_name, p_function->parameters[i]->identifier->name);
		}
		is_shadowing(p_function->parameters[i]->identifier, "function parameter", true);
#endif // DEBUG_ENABLED

		if (p_function->parameters[i]->initializer) {
#ifdef TOOLS_ENABLED
			default_value_count++;
#endif // TOOLS_ENABLED

			if (p_function->parameters[i]->initializer->is_constant) {
				p_function->default_arg_values.push_back(p_function->parameters[i]->initializer->reduced_value);
			} else {
				p_function->default_arg_values.push_back(Variant()); // Prevent shift.
			}
		}
	}

	if (p_function->is_vararg()) {
		resolve_parameter(p_function->rest_parameter);
		if (p_function->rest_parameter->datatype_specifier != nullptr) {
			GDScriptParser::DataType specified_type = p_function->rest_parameter->get_datatype();
			if (specified_type.kind != GDScriptParser::DataType::BUILTIN || specified_type.builtin_type != Variant::ARRAY) {
				push_error(vformat(R"(The rest parameter type must be "Array", but "%s" is specified.)", specified_type.to_string()), p_function->rest_parameter->datatype_specifier);
			} else if ((specified_type.has_container_element_type(0) && !specified_type.get_container_element_type(0).is_variant())) {
				push_error(R"(Typed arrays are currently not supported for the rest parameter.)", p_function->rest_parameter->datatype_specifier);
			}
		} else {
			GDScriptParser::DataType inferred_type;
			inferred_type.type_source = GDScriptParser::DataType::INFERRED;
			inferred_type.kind = GDScriptParser::DataType::BUILTIN;
			inferred_type.builtin_type = Variant::ARRAY;
			p_function->rest_parameter->set_datatype(inferred_type);
#ifdef DEBUG_ENABLED
			parser->push_warning(p_function->rest_parameter, GDScriptWarning::UNTYPED_DECLARATION, "Parameter", p_function->rest_parameter->identifier->name);
#endif
		}
#ifdef DEBUG_ENABLED
		if (p_function->rest_parameter->usages == 0 && !String(p_function->rest_parameter->identifier->name).begins_with("_") && !p_function->is_abstract) {
			parser->push_warning(p_function->rest_parameter->identifier, GDScriptWarning::UNUSED_PARAMETER, function_visible_name, p_function->rest_parameter->identifier->name);
		}
		is_shadowing(p_function->rest_parameter->identifier, "function parameter", true);
#endif // DEBUG_ENABLED
	}

	if (!p_is_lambda && function_name == GDScriptLanguage::get_singleton()->strings._init) {
		// Constructor.
		GDScriptParser::DataType return_type = parser->current_class->get_datatype();
		return_type.is_meta_type = false;
		p_function->set_datatype(return_type);
		if (p_function->return_type) {
			GDScriptParser::DataType declared_return = resolve_datatype(p_function->return_type);
			if (declared_return.kind != GDScriptParser::DataType::BUILTIN || declared_return.builtin_type != Variant::NIL) {
				push_error("Constructor cannot have an explicit return type.", p_function->return_type);
			}
		}
	} else if (!p_is_lambda && function_name == GDScriptLanguage::get_singleton()->strings._static_init) {
		// Static constructor.
		GDScriptParser::DataType return_type;
		return_type.kind = GDScriptParser::DataType::BUILTIN;
		return_type.builtin_type = Variant::NIL;
		p_function->set_datatype(return_type);
		if (p_function->return_type) {
			GDScriptParser::DataType declared_return = resolve_datatype(p_function->return_type);
			if (declared_return.kind != GDScriptParser::DataType::BUILTIN || declared_return.builtin_type != Variant::NIL) {
				push_error("Static constructor cannot have an explicit return type.", p_function->return_type);
			}
		}
	} else {
		if (p_function->return_type != nullptr) {
			p_function->set_datatype(type_from_metatype(resolve_datatype(p_function->return_type)));
		} else {
			// In case the function is not typed, we can safely assume it's a Variant, so it's okay to mark as "inferred" here.
			// It's not "undetected" to not mix up with unknown functions.
			GDScriptParser::DataType return_type;
			return_type.type_source = GDScriptParser::DataType::INFERRED;
			return_type.kind = GDScriptParser::DataType::VARIANT;
			p_function->set_datatype(return_type);
		}

#ifdef TOOLS_ENABLED
		// Check if the function signature matches the parent. If not it's an error since it breaks polymorphism.
		// Not for the constructor which can vary in signature.
		GDScriptParser::DataType base_type = parser->current_class->base_type;
		base_type.is_meta_type = false;
		GDScriptParser::DataType parent_return_type;
		List<GDScriptParser::DataType> parameters_types;
		int default_par_count = 0;
		BitField<MethodFlags> method_flags = {};
		StringName native_base;
		if (!p_is_lambda && get_function_signature(p_function, false, base_type, function_name, parent_return_type, parameters_types, default_par_count, method_flags, &native_base)) {
			bool valid = p_function->is_static == method_flags.has_flag(METHOD_FLAG_STATIC);
			const bool parent_is_coroutine = method_flags.has_flag(METHOD_FLAG_ASYNC);
			const bool current_is_coroutine = p_function->is_coroutine;
			const bool valid_coroutine_override = parent_is_coroutine == current_is_coroutine;
			valid = valid && valid_coroutine_override;

			if (p_function->return_type != nullptr) {
				// Check return type covariance.
				GDScriptParser::DataType return_type = p_function->get_datatype();
				if (return_type.is_variant()) {
					// `is_type_compatible()` returns `true` if one of the types is `Variant`.
					// Don't allow an explicitly specified `Variant` if the parent return type is narrower.
					valid = valid && parent_return_type.is_variant();
				} else if (return_type.kind == GDScriptParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
					// `is_type_compatible()` returns `true` if target is an `Object` and source is `null`.
					// Don't allow `void` if the parent return type is a hard non-`void` type.
					if (parent_return_type.is_hard_type() && !(parent_return_type.kind == GDScriptParser::DataType::BUILTIN && parent_return_type.builtin_type == Variant::NIL)) {
						valid = false;
					}
				} else if (parent_return_type.is_set() && return_type.is_set()) {
					// Skip while a type is still resolving (re-entrant member resolution);
					// `is_type_compatible()` treats an unset type as compatible anyway.
					valid = valid && is_type_compatible(parent_return_type, return_type);
				}
			}

			int parent_min_argc = parameters_types.size() - default_par_count;
			int parent_max_argc = (method_flags & METHOD_FLAG_VARARG) ? INT_MAX : parameters_types.size();
			int current_min_argc = p_function->parameters.size() - default_value_count;
			int current_max_argc = p_function->is_vararg() ? INT_MAX : p_function->parameters.size();

			// `[current_min_argc..current_max_argc]` must include `[parent_min_argc..parent_max_argc]`.
			valid = valid && current_min_argc <= parent_min_argc && parent_max_argc <= current_max_argc;

			if (valid) {
				int i = 0;
				for (const GDScriptParser::DataType &parent_par_type : parameters_types) {
					if (i >= p_function->parameters.size()) {
						break;
					}
					const GDScriptParser::DataType &current_par_type = p_function->parameters[i]->datatype;
					i++;
					// Check parameter type contravariance.
					if (parent_par_type.is_variant() && parent_par_type.is_hard_type()) {
						// `is_type_compatible()` returns `true` if one of the types is `Variant`.
						// Don't allow narrowing a hard `Variant`.
						valid = valid && current_par_type.is_variant();
					} else if (current_par_type.is_set() && parent_par_type.is_set()) {
						// Skip while a type is still resolving (re-entrant member resolution);
						// `is_type_compatible()` treats an unset type as compatible anyway.
						valid = valid && is_type_compatible(current_par_type, parent_par_type);
					}
				}
			}

			if (!valid_coroutine_override) {
				if (parent_is_coroutine) {
					push_error(vformat(R"*(The function "%s()" must be async because it overrides an async parent function.)*", function_name), p_function);
				} else {
					push_error(vformat(R"*(The function "%s()" cannot be async because it overrides a synchronous parent function.)*", function_name), p_function);
				}
			} else if (!valid) {
				// Compute parent signature as a string to show in the error message.
				String parent_signature = String(function_name) + "(";
				int j = 0;
				for (const GDScriptParser::DataType &par_type : parameters_types) {
					if (j > 0) {
						parent_signature += ", ";
					}
					String parameter = par_type.to_string();
					if (parameter == "null") {
						parameter = "Variant";
					}
					parent_signature += parameter;
					if (j >= parameters_types.size() - default_par_count) {
						parent_signature += " = <default>";
					}

					j++;
				}
				if (method_flags & METHOD_FLAG_VARARG) {
					if (!parameters_types.is_empty()) {
						parent_signature += ", ";
					}
					parent_signature += "...";
				}
				parent_signature += ") -> ";

				const String return_type = parent_return_type.to_string_strict();
				if (return_type == "null") {
					parent_signature += "void";
				} else {
					parent_signature += return_type;
				}

				push_error(vformat(R"(The function signature doesn't match the parent. Parent signature is "%s".)", parent_signature), p_function);
			}
#ifdef DEBUG_ENABLED
			if (native_base != StringName() && !(parser->current_class->is_trait && p_function->is_abstract)) {
				parser->push_warning(p_function, GDScriptWarning::NATIVE_METHOD_OVERRIDE, function_name, native_base);
			}
#endif // DEBUG_ENABLED
		}
#endif // TOOLS_ENABLED
	}

#ifdef DEBUG_ENABLED
	if (p_function->return_type == nullptr) {
		parser->push_warning(p_function, GDScriptWarning::UNTYPED_DECLARATION, "Function", function_visible_name);
	}
#endif // DEBUG_ENABLED

	method_info.default_arguments.append_array(p_function->default_arg_values);
	method_info.return_val = p_function->get_datatype().to_property_info("");
	p_function->info = method_info;

	if (p_function->get_datatype().is_resolving()) {
		p_function->set_datatype(prev_datatype);
	}

	parser->current_function = previous_function;
	static_context = previous_static_context;
}

void GDScriptAnalyzer::resolve_function_body(GDScriptParser::FunctionNode *p_function, bool p_is_lambda) {
	if (p_function->resolved_body) {
		return;
	}
	p_function->resolved_body = true;

	if (p_function->body->statements.is_empty()) {
		// Non-abstract functions must have a body.
		if (p_function->source_lambda != nullptr) {
			push_error(R"(A lambda function must have a ":" followed by a body.)", p_function);
		} else if (!p_function->is_abstract) {
			push_error(R"(A function must either have a ":" followed by a body, or be marked as "@abstract".)", p_function);
		}
		return;
	} else {
		// Abstract functions must not have a body.
		if (p_function->is_abstract) {
			push_error(R"(An abstract function cannot have a body.)", p_function->body);
			return;
		}
	}

	GDScriptParser::FunctionNode *previous_function = parser->current_function;
	parser->current_function = p_function;

	bool previous_static_context = static_context;
	static_context = p_function->is_static;

	HashMap<const GDScriptParser::Node *, GDScriptParser::DataType> previous_flow_narrowed_types = flow_narrowed_types;
	HashMap<const GDScriptParser::Node *, bool> previous_flow_narrowing_captured_sources;
	if (!p_is_lambda) {
		previous_flow_narrowing_captured_sources = flow_narrowing_captured_sources;
		flow_narrowing_captured_sources.clear();
	}
	flow_narrowed_types.clear();
	resolve_suite(p_function->body);
	flow_narrowed_types = previous_flow_narrowed_types;
	if (!p_is_lambda) {
		flow_narrowing_captured_sources = previous_flow_narrowing_captured_sources;
	}

	const SuiteExitState body_exit = get_suite_exit_state(p_function->body);
	warn_unreachable_after_noreturn(p_function->body);

	if (p_function->is_noreturn) {
		if (body_exit.has_return) {
			push_error(R"(A "@noreturn" function cannot return.)", p_function);
		} else if (!body_exit.always_terminates) {
			push_error(R"(A "@noreturn" function cannot complete normally.)", p_function);
		}
	}

	if (!p_function->get_datatype().is_hard_type() && p_function->body->get_datatype().is_set()) {
		// Use the suite inferred type if return isn't explicitly set.
		p_function->set_datatype(p_function->body->get_datatype());
	} else if (p_function->get_datatype().is_hard_type() && (p_function->get_datatype().kind != GDScriptParser::DataType::BUILTIN || p_function->get_datatype().builtin_type != Variant::NIL)) {
		if (!body_exit.always_terminates && (p_is_lambda || p_function->identifier->name != GDScriptLanguage::get_singleton()->strings._init)) {
			push_error(R"(Not all code paths return a value.)", p_function);
		}
	}

	parser->current_function = previous_function;
	static_context = previous_static_context;
}

GDScriptAnalyzer::SuiteExitState GDScriptAnalyzer::get_suite_exit_state(const GDScriptParser::SuiteNode *p_suite) const {
	SuiteExitState result;
	if (p_suite == nullptr) {
		return result;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		const SuiteExitState statement_exit = get_statement_exit_state(statement);
		result.has_return = result.has_return || statement_exit.has_return;
		result.has_noreturn = result.has_noreturn || statement_exit.has_noreturn;

		if (statement_exit.always_terminates) {
			result.always_terminates = true;
			return result;
		}
	}

	return result;
}

GDScriptAnalyzer::SuiteExitState GDScriptAnalyzer::get_statement_exit_state(const GDScriptParser::Node *p_statement) const {
	SuiteExitState result;
	if (p_statement == nullptr) {
		return result;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::RETURN:
			result.always_terminates = true;
			result.has_return = true;
			break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_statement);
			if (call->is_noreturn) {
				result.always_terminates = true;
				result.has_noreturn = true;
			}
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			const SuiteExitState true_exit = get_suite_exit_state(if_node->true_block);
			const SuiteExitState false_exit = get_suite_exit_state(if_node->false_block);

			result.has_return = true_exit.has_return || false_exit.has_return;
			result.has_noreturn = true_exit.has_noreturn || false_exit.has_noreturn;
			result.always_terminates = if_node->false_block != nullptr && true_exit.always_terminates && false_exit.always_terminates;
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			bool all_branches_terminate = !match_node->branches.is_empty();
			bool has_wildcard = false;
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				const SuiteExitState branch_exit = get_suite_exit_state(branch->block);
				result.has_return = result.has_return || branch_exit.has_return;
				result.has_noreturn = result.has_noreturn || branch_exit.has_noreturn;
				all_branches_terminate = all_branches_terminate && branch_exit.always_terminates;
				has_wildcard = has_wildcard || branch->has_wildcard;
			}
			result.always_terminates = has_wildcard && all_branches_terminate;
		} break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_statement);
			const SuiteExitState loop_exit = get_suite_exit_state(while_node->loop);
			result.has_return = loop_exit.has_return;
			result.has_noreturn = loop_exit.has_noreturn;
			result.always_terminates = while_node->condition != nullptr && while_node->condition->is_constant &&
					while_node->condition->reduced_value.booleanize() && !suite_has_reachable_break(while_node->loop);
		} break;
		case GDScriptParser::Node::SUITE:
			result = get_suite_exit_state(static_cast<const GDScriptParser::SuiteNode *>(p_statement));
			break;
		default:
			break;
	}

	return result;
}

bool GDScriptAnalyzer::suite_has_reachable_break(const GDScriptParser::SuiteNode *p_suite) const {
	if (p_suite == nullptr) {
		return false;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement_has_reachable_break(statement)) {
			return true;
		}
		if (get_statement_exit_state(statement).always_terminates) {
			return false;
		}
	}
	return false;
}

bool GDScriptAnalyzer::statement_has_reachable_break(const GDScriptParser::Node *p_statement) const {
	if (p_statement == nullptr) {
		return false;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::BREAK:
			return true;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			return suite_has_reachable_break(if_node->true_block) || suite_has_reachable_break(if_node->false_block);
		}
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				if (suite_has_reachable_break(branch->block)) {
					return true;
				}
			}
			return false;
		}
		case GDScriptParser::Node::SUITE:
			return suite_has_reachable_break(static_cast<const GDScriptParser::SuiteNode *>(p_statement));
		default:
			return false;
	}
}

void GDScriptAnalyzer::warn_unreachable_after_noreturn(const GDScriptParser::SuiteNode *p_suite) {
#ifdef DEBUG_ENABLED
	if (p_suite == nullptr) {
		return;
	}

	for (int i = 0; i < p_suite->statements.size(); i++) {
		const GDScriptParser::Node *statement = p_suite->statements[i];
		warn_unreachable_after_noreturn_in_statement(statement);

		const SuiteExitState statement_exit = get_statement_exit_state(statement);
		if (statement_exit.always_terminates) {
			if (statement_exit.has_noreturn && i + 1 < p_suite->statements.size()) {
				const StringName function_name = parser->current_function && parser->current_function->identifier ? parser->current_function->identifier->name : StringName("<anonymous lambda>");
				parser->push_warning(p_suite->statements[i + 1], GDScriptWarning::UNREACHABLE_CODE, function_name);
			}
			return;
		}
	}
#else
	(void)p_suite;
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::warn_unreachable_after_noreturn_in_statement(const GDScriptParser::Node *p_statement) {
#ifdef DEBUG_ENABLED
	if (p_statement == nullptr) {
		return;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			warn_unreachable_after_noreturn(if_node->true_block);
			warn_unreachable_after_noreturn(if_node->false_block);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				warn_unreachable_after_noreturn(branch->block);
			}
		} break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_statement);
			warn_unreachable_after_noreturn(while_node->loop);
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_statement);
			warn_unreachable_after_noreturn(for_node->loop);
		} break;
		case GDScriptParser::Node::SUITE:
			warn_unreachable_after_noreturn(static_cast<const GDScriptParser::SuiteNode *>(p_statement));
			break;
		default:
			break;
	}
#else
	(void)p_statement;
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::decide_suite_type(GDScriptParser::Node *p_suite, GDScriptParser::Node *p_statement) {
	if (p_statement == nullptr) {
		return;
	}
	switch (p_statement->type) {
		case GDScriptParser::Node::IF:
		case GDScriptParser::Node::FOR:
		case GDScriptParser::Node::MATCH:
		case GDScriptParser::Node::PATTERN:
		case GDScriptParser::Node::RETURN:
		case GDScriptParser::Node::WHILE:
			// Use return or nested suite type as this suite type.
			if (p_suite->get_datatype().is_set() && (p_suite->get_datatype() != p_statement->get_datatype())) {
				// Mixed types.
				// TODO: This could use the common supertype instead.
				p_suite->datatype.kind = GDScriptParser::DataType::VARIANT;
				p_suite->datatype.type_source = GDScriptParser::DataType::UNDETECTED;
			} else {
				p_suite->set_datatype(p_statement->get_datatype());
				p_suite->datatype.type_source = GDScriptParser::DataType::INFERRED;
			}
			break;
		default:
			break;
	}
}

void GDScriptAnalyzer::resolve_suite(GDScriptParser::SuiteNode *p_suite, bool p_is_root) {
	for (int i = 0; i < p_suite->statements.size(); i++) {
		GDScriptParser::Node *stmt = p_suite->statements[i];
		// Apply annotations.
		for (GDScriptParser::AnnotationNode *&E : stmt->annotations) {
			resolve_annotation(E);
			E->apply(parser, stmt, nullptr); // TODO: Provide `p_class`.
		}

		resolve_node(stmt, p_is_root);
		resolve_pending_lambda_bodies();
		decide_suite_type(p_suite, stmt);
	}
}

void GDScriptAnalyzer::resolve_assignable(GDScriptParser::AssignableNode *p_assignable, const char *p_kind) {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::VARIANT;

	bool is_constant = p_assignable->type == GDScriptParser::Node::CONSTANT;

#ifdef DEBUG_ENABLED
	if (p_assignable->identifier != nullptr && p_assignable->identifier->suite != nullptr && p_assignable->identifier->suite->parent_block != nullptr) {
		if (p_assignable->identifier->suite->parent_block->has_local(p_assignable->identifier->name)) {
			const GDScriptParser::SuiteNode::Local &local = p_assignable->identifier->suite->parent_block->get_local(p_assignable->identifier->name);
			parser->push_warning(p_assignable->identifier, GDScriptWarning::CONFUSABLE_LOCAL_DECLARATION, local.get_name(), p_assignable->identifier->name);
		}
	}
#endif // DEBUG_ENABLED

	GDScriptParser::DataType specified_type;
	bool has_specified_type = p_assignable->datatype_specifier != nullptr;
	if (has_specified_type) {
		specified_type = type_from_metatype(resolve_datatype(p_assignable->datatype_specifier));
		type = specified_type;
	}

	if (p_assignable->initializer != nullptr) {
		reduce_expression(p_assignable->initializer);

		if (p_assignable->initializer->type == GDScriptParser::Node::ARRAY) {
			GDScriptParser::ArrayNode *array = static_cast<GDScriptParser::ArrayNode *>(p_assignable->initializer);
			if (has_specified_type && specified_type.has_container_element_type(0)) {
				update_array_literal_element_type(array, specified_type.get_container_element_type(0));
			}
		} else if (p_assignable->initializer->type == GDScriptParser::Node::DICTIONARY) {
			GDScriptParser::DictionaryNode *dictionary = static_cast<GDScriptParser::DictionaryNode *>(p_assignable->initializer);
			if (has_specified_type && specified_type.has_container_element_types()) {
				update_dictionary_literal_element_type(dictionary, specified_type.get_container_element_type_or_variant(0), specified_type.get_container_element_type_or_variant(1));
			}
		}

		if (is_constant && !p_assignable->initializer->is_constant) {
			bool is_initializer_value_reduced = false;
			Variant initializer_value = make_expression_reduced_value(p_assignable->initializer, is_initializer_value_reduced);
			if (is_initializer_value_reduced) {
				p_assignable->initializer->is_constant = true;
				p_assignable->initializer->reduced_value = initializer_value;
			} else {
				push_error(vformat(R"(Assigned value for %s "%s" isn't a constant expression.)", p_kind, p_assignable->identifier->name), p_assignable->initializer);
			}
		}

		if (has_specified_type && p_assignable->initializer->is_constant) {
			update_const_expression_builtin_type(p_assignable->initializer, specified_type, "assign");
		}
		GDScriptParser::DataType initializer_type = p_assignable->initializer->get_datatype();

		if (p_assignable->infer_datatype) {
			if (!initializer_type.is_set() || initializer_type.has_no_type() || !initializer_type.is_hard_type()) {
				push_error(vformat(R"(Cannot infer the type of "%s" %s because the value doesn't have a set type.)", p_assignable->identifier->name, p_kind), p_assignable->initializer);
			} else if (initializer_type.kind == GDScriptParser::DataType::BUILTIN && initializer_type.builtin_type == Variant::NIL && !is_constant) {
				push_error(vformat(R"(Cannot infer the type of "%s" %s because the value is "null".)", p_assignable->identifier->name, p_kind), p_assignable->initializer);
			}
#ifdef DEBUG_ENABLED
			if (initializer_type.is_hard_type() && initializer_type.is_variant()) {
				parser->push_warning(p_assignable, GDScriptWarning::INFERENCE_ON_VARIANT, p_kind);
			}
#endif // DEBUG_ENABLED
		} else {
			if (!initializer_type.is_set()) {
				push_error(vformat(R"(Could not resolve type for %s "%s".)", p_kind, p_assignable->identifier->name), p_assignable->initializer);
			}
		}

		if (!has_specified_type) {
			type = initializer_type;

			if (!type.is_set() || (type.is_hard_type() && type.kind == GDScriptParser::DataType::BUILTIN && type.builtin_type == Variant::NIL && !is_constant)) {
				type.kind = GDScriptParser::DataType::VARIANT;
			}

			if (p_assignable->infer_datatype || is_constant) {
				type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
			} else {
				type.type_source = GDScriptParser::DataType::INFERRED;
			}
		} else if (!specified_type.is_variant()) {
			if (initializer_type.is_variant() || !initializer_type.is_hard_type()) {
				if (initializer_type.is_variant() && strict_dynamic_checks) {
					push_error(vformat(R"(Cannot assign Variant value to %s "%s" in strict dynamic mode; expected "%s".)",
									   p_kind,
									   p_assignable->identifier->name,
									   specified_type.to_string()),
							p_assignable->initializer);
				} else {
					mark_node_unsafe(p_assignable->initializer);
					p_assignable->use_conversion_assign = true;
				}
				if (!initializer_type.is_variant() && !is_type_compatible(specified_type, initializer_type, true, p_assignable->initializer)) {
					downgrade_node_type_source(p_assignable->initializer);
				}
			} else if (!is_type_compatible(specified_type, initializer_type, true, p_assignable->initializer)) {
				const bool nullable_mismatch = strict_null_checks && initializer_type.is_nullable && !specified_type.is_nullable && !specified_type.is_variant();
				if (!nullable_mismatch && !is_constant && is_type_compatible(initializer_type, specified_type)) {
					mark_node_unsafe(p_assignable->initializer);
					p_assignable->use_conversion_assign = true;
				} else {
					if (nullable_mismatch) {
						push_error(vformat(R"(Cannot assign nullable value of type "%s" to %s "%s"; expected non-nullable "%s".)",
										   initializer_type.to_string(),
										   p_kind,
										   p_assignable->identifier->name,
										   specified_type.to_string()),
								p_assignable->initializer);
					} else {
						push_error(vformat(R"(Cannot assign a value of type %s to %s "%s" with specified type %s.)",
										   initializer_type.to_string(),
										   p_kind,
										   p_assignable->identifier->name,
										   specified_type.to_string()),
								p_assignable->initializer);
					}
				}
			} else if ((specified_type.has_container_element_type(0) && !initializer_type.has_container_element_type(0)) || (specified_type.has_container_element_type(1) && !initializer_type.has_container_element_type(1))) {
				mark_node_unsafe(p_assignable->initializer);
#ifdef DEBUG_ENABLED
			} else if (specified_type.builtin_type == Variant::INT && initializer_type.builtin_type == Variant::FLOAT) {
				parser->push_warning(p_assignable->initializer, GDScriptWarning::NARROWING_CONVERSION);
#endif // DEBUG_ENABLED
			}
		}
	}

#ifdef DEBUG_ENABLED
	const bool is_parameter = p_assignable->type == GDScriptParser::Node::PARAMETER;
	if (!has_specified_type) {
		const String declaration_type = is_constant ? "Constant" : (is_parameter ? "Parameter" : "Variable");
		if (p_assignable->infer_datatype || is_constant) {
			// Do not produce the `INFERRED_DECLARATION` warning on type import because there is no way to specify the true type.
			// And removing the metatype makes it impossible to use the constant as a type hint (especially for enums).
			const bool is_type_import = is_constant && p_assignable->initializer != nullptr && p_assignable->initializer->datatype.is_meta_type;
			if (!is_type_import) {
				parser->push_warning(p_assignable, GDScriptWarning::INFERRED_DECLARATION, declaration_type, p_assignable->identifier->name);
			}
		} else {
			parser->push_warning(p_assignable, GDScriptWarning::UNTYPED_DECLARATION, declaration_type, p_assignable->identifier->name);
		}
	} else if (!is_parameter && specified_type.kind == GDScriptParser::DataType::ENUM && p_assignable->initializer == nullptr) {
		// Warn about enum variables without default value. Unless the enum defines the "0" value, then it's fine.
		bool has_zero_value = false;
		for (const KeyValue<StringName, int64_t> &kv : specified_type.enum_values) {
			if (kv.value == 0) {
				has_zero_value = true;
				break;
			}
		}
		if (!has_zero_value) {
			parser->push_warning(p_assignable, GDScriptWarning::ENUM_VARIABLE_WITHOUT_DEFAULT, p_assignable->identifier->name);
		}
	}
#endif // DEBUG_ENABLED

	type.is_constant = is_constant;
	type.is_read_only = false;
	p_assignable->set_datatype(type);
}

void GDScriptAnalyzer::resolve_variable(GDScriptParser::VariableNode *p_variable, bool p_is_local) {
	static constexpr const char *kind = "variable";
	resolve_assignable(p_variable, kind);

#ifdef DEBUG_ENABLED
	if (p_is_local) {
		if (p_variable->usages == 0 && !String(p_variable->identifier->name).begins_with("_")) {
			parser->push_warning(p_variable, GDScriptWarning::UNUSED_VARIABLE, p_variable->identifier->name);
		}
	}
	is_shadowing(p_variable->identifier, kind, p_is_local);
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::resolve_constant(GDScriptParser::ConstantNode *p_constant, bool p_is_local) {
	static constexpr const char *kind = "constant";
	resolve_assignable(p_constant, kind);

#ifdef DEBUG_ENABLED
	if (p_is_local) {
		if (p_constant->usages == 0 && !String(p_constant->identifier->name).begins_with("_")) {
			parser->push_warning(p_constant, GDScriptWarning::UNUSED_LOCAL_CONSTANT, p_constant->identifier->name);
		}
	}
	is_shadowing(p_constant->identifier, kind, p_is_local);
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::resolve_parameter(GDScriptParser::ParameterNode *p_parameter) {
	static constexpr const char *kind = "parameter";
	resolve_assignable(p_parameter, kind);
}

const GDScriptParser::Node *GDScriptAnalyzer::flow_narrowing_key_from_identifier(const GDScriptParser::IdentifierNode *p_identifier) const {
	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			return p_identifier->parameter_source;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			return p_identifier->variable_source;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			return p_identifier->bind_source;
		case GDScriptParser::IdentifierNode::UNDEFINED_SOURCE:
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
		case GDScriptParser::IdentifierNode::MEMBER_CONSTANT:
		case GDScriptParser::IdentifierNode::MEMBER_FUNCTION:
		case GDScriptParser::IdentifierNode::MEMBER_SIGNAL:
		case GDScriptParser::IdentifierNode::MEMBER_CLASS:
		case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE:
		case GDScriptParser::IdentifierNode::NATIVE_CLASS:
			return nullptr;
	}

	return nullptr;
}

void GDScriptAnalyzer::apply_flow_narrowing(const GDScriptParser::IdentifierNode *p_identifier) {
	const GDScriptParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key == nullptr) {
		return;
	}

	GDScriptParser::DataType narrowed_type = p_identifier->get_datatype();
	if (!narrowed_type.is_nullable) {
		return;
	}

	narrowed_type.is_nullable = false;
	flow_narrowed_types[key] = narrowed_type;
}

void GDScriptAnalyzer::apply_flow_narrowing(const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::DataType &p_type) {
	const GDScriptParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key == nullptr || !p_type.is_set()) {
		return;
	}

	GDScriptParser::DataType narrowed_type = p_type;
	narrowed_type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
	flow_narrowed_types[key] = narrowed_type;
}

void GDScriptAnalyzer::clear_flow_narrowing(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != GDScriptParser::Node::IDENTIFIER) {
		return;
	}

	const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
	const GDScriptParser::Node *key = flow_narrowing_key_from_identifier(identifier);
	if (key != nullptr) {
		flow_narrowed_types.erase(key);
	}
}

void GDScriptAnalyzer::mark_flow_narrowing_capture(const GDScriptParser::IdentifierNode *p_identifier) {
	const GDScriptParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key != nullptr) {
		flow_narrowing_captured_sources[key] = true;
	}
}

void GDScriptAnalyzer::clear_captured_flow_narrowing() {
	for (const KeyValue<const GDScriptParser::Node *, bool> &E : flow_narrowing_captured_sources) {
		flow_narrowed_types.erase(E.key);
	}
}

static bool _is_null_literal(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != GDScriptParser::Node::LITERAL) {
		return false;
	}

	const GDScriptParser::LiteralNode *literal = static_cast<const GDScriptParser::LiteralNode *>(p_expression);
	return literal->value.get_type() == Variant::NIL;
}

bool GDScriptAnalyzer::null_check_narrowing_identifier(GDScriptParser::ExpressionNode *p_condition, bool p_condition_value, GDScriptParser::IdentifierNode *&r_identifier) const {
	r_identifier = nullptr;
	if (p_condition == nullptr || p_condition->type != GDScriptParser::Node::BINARY_OPERATOR) {
		return false;
	}

	GDScriptParser::BinaryOpNode *binary_op = static_cast<GDScriptParser::BinaryOpNode *>(p_condition);
	if (binary_op->variant_op != Variant::OP_EQUAL && binary_op->variant_op != Variant::OP_NOT_EQUAL) {
		return false;
	}

	const bool condition_true_means_not_null = binary_op->variant_op == Variant::OP_NOT_EQUAL;
	if (p_condition_value != condition_true_means_not_null) {
		return false;
	}

	GDScriptParser::ExpressionNode *candidate = nullptr;
	if (_is_null_literal(binary_op->left_operand)) {
		candidate = binary_op->right_operand;
	} else if (_is_null_literal(binary_op->right_operand)) {
		candidate = binary_op->left_operand;
	}
	if (candidate == nullptr || candidate->type != GDScriptParser::Node::IDENTIFIER) {
		return false;
	}

	GDScriptParser::IdentifierNode *identifier = static_cast<GDScriptParser::IdentifierNode *>(candidate);
	if (flow_narrowing_key_from_identifier(identifier) == nullptr || !identifier->get_datatype().is_nullable) {
		return false;
	}

	r_identifier = identifier;
	return true;
}

bool GDScriptAnalyzer::type_test_narrowing_identifier(GDScriptParser::ExpressionNode *p_condition, bool p_condition_value, GDScriptParser::IdentifierNode *&r_identifier, GDScriptParser::DataType &r_type) const {
	r_identifier = nullptr;
	r_type = GDScriptParser::DataType();

	bool condition_true_means_type_match = true;
	GDScriptParser::ExpressionNode *condition = p_condition;
	if (condition != nullptr && condition->type == GDScriptParser::Node::UNARY_OPERATOR) {
		GDScriptParser::UnaryOpNode *unary_op = static_cast<GDScriptParser::UnaryOpNode *>(condition);
		if (unary_op->variant_op != Variant::OP_NOT) {
			return false;
		}
		condition_true_means_type_match = false;
		condition = unary_op->operand;
	}

	if (p_condition_value != condition_true_means_type_match || condition == nullptr || condition->type != GDScriptParser::Node::TYPE_TEST) {
		return false;
	}

	GDScriptParser::TypeTestNode *type_test = static_cast<GDScriptParser::TypeTestNode *>(condition);
	if (type_test->operand == nullptr || type_test->operand->type != GDScriptParser::Node::IDENTIFIER || !type_test->test_datatype.is_set()) {
		return false;
	}

	GDScriptParser::IdentifierNode *identifier = static_cast<GDScriptParser::IdentifierNode *>(type_test->operand);
	if (flow_narrowing_key_from_identifier(identifier) == nullptr) {
		return false;
	}

	r_identifier = identifier;
	r_type = type_test->test_datatype;
	return true;
}

void GDScriptAnalyzer::resolve_if(GDScriptParser::IfNode *p_if) {
	reduce_expression(p_if->condition);

	HashMap<const GDScriptParser::Node *, GDScriptParser::DataType> previous_flow_narrowed_types = flow_narrowed_types;
	GDScriptParser::IdentifierNode *narrowed_identifier = nullptr;
	if (null_check_narrowing_identifier(p_if->condition, true, narrowed_identifier)) {
		apply_flow_narrowing(narrowed_identifier);
	} else {
		GDScriptParser::DataType narrowed_type;
		if (type_test_narrowing_identifier(p_if->condition, true, narrowed_identifier, narrowed_type)) {
			apply_flow_narrowing(narrowed_identifier, narrowed_type);
		}
	}
	resolve_suite(p_if->true_block);
	flow_narrowed_types = previous_flow_narrowed_types;
	p_if->set_datatype(p_if->true_block->get_datatype());

	if (p_if->false_block != nullptr) {
		previous_flow_narrowed_types = flow_narrowed_types;
		if (null_check_narrowing_identifier(p_if->condition, false, narrowed_identifier)) {
			apply_flow_narrowing(narrowed_identifier);
		} else {
			GDScriptParser::DataType narrowed_type;
			if (type_test_narrowing_identifier(p_if->condition, false, narrowed_identifier, narrowed_type)) {
				apply_flow_narrowing(narrowed_identifier, narrowed_type);
			}
		}
		resolve_suite(p_if->false_block);
		flow_narrowed_types = previous_flow_narrowed_types;
		decide_suite_type(p_if, p_if->false_block);
	}
}

void GDScriptAnalyzer::resolve_for(GDScriptParser::ForNode *p_for) {
	GDScriptParser::DataType variable_type;
	GDScriptParser::DataType list_type;

	if (p_for->list) {
		resolve_node(p_for->list, false);

		bool is_range = false;
		if (p_for->list->type == GDScriptParser::Node::CALL) {
			GDScriptParser::CallNode *call = static_cast<GDScriptParser::CallNode *>(p_for->list);
			if (call->get_callee_type() == GDScriptParser::Node::IDENTIFIER) {
				if (static_cast<GDScriptParser::IdentifierNode *>(call->callee)->name == "range") {
					if (call->arguments.is_empty()) {
						push_error(R"*(Invalid call for "range()" function. Expected at least 1 argument, none given.)*", call);
					} else if (call->arguments.size() > 3) {
						push_error(vformat(R"*(Invalid call for "range()" function. Expected at most 3 arguments, %d given.)*", call->arguments.size()), call);
					}
					is_range = true;
					variable_type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
					variable_type.kind = GDScriptParser::DataType::BUILTIN;
					variable_type.builtin_type = Variant::INT;
				}
			}
		}

		list_type = p_for->list->get_datatype();

		if (!list_type.is_hard_type()) {
			mark_node_unsafe(p_for->list);
		}

		if (is_range) {
			// Already solved.
		} else if (list_type.is_variant()) {
			variable_type.kind = GDScriptParser::DataType::VARIANT;
			mark_node_unsafe(p_for->list);
		} else if (list_type.has_container_element_type(0)) {
			variable_type = list_type.get_container_element_type(0);
			variable_type.type_source = list_type.type_source;
		} else if (list_type.is_typed_container_type()) {
			variable_type = list_type.get_typed_container_type();
			variable_type.type_source = list_type.type_source;
		} else if (list_type.builtin_type == Variant::INT || list_type.builtin_type == Variant::FLOAT || list_type.builtin_type == Variant::STRING) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = GDScriptParser::DataType::BUILTIN;
			variable_type.builtin_type = list_type.builtin_type;
		} else if (list_type.builtin_type == Variant::VECTOR2I || list_type.builtin_type == Variant::VECTOR3I) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = GDScriptParser::DataType::BUILTIN;
			variable_type.builtin_type = Variant::INT;
		} else if (list_type.builtin_type == Variant::VECTOR2 || list_type.builtin_type == Variant::VECTOR3) {
			variable_type.type_source = list_type.type_source;
			variable_type.kind = GDScriptParser::DataType::BUILTIN;
			variable_type.builtin_type = Variant::FLOAT;
		} else if (list_type.builtin_type == Variant::OBJECT) {
			GDScriptParser::DataType return_type;
			List<GDScriptParser::DataType> par_types;
			int default_arg_count = 0;
			BitField<MethodFlags> method_flags = {};
			if (get_function_signature(p_for->list, false, list_type, CoreStringName(_iter_get), return_type, par_types, default_arg_count, method_flags)) {
				variable_type = return_type;
				variable_type.type_source = list_type.type_source;
			} else if (!list_type.is_hard_type()) {
				variable_type.kind = GDScriptParser::DataType::VARIANT;
			} else {
				push_error(vformat(R"(Unable to iterate on object of type "%s".)", list_type.to_string()), p_for->list);
			}
		} else if (list_type.builtin_type == Variant::ARRAY || list_type.builtin_type == Variant::DICTIONARY || !list_type.is_hard_type()) {
			variable_type.kind = GDScriptParser::DataType::VARIANT;
		} else {
			push_error(vformat(R"(Unable to iterate on value of type "%s".)", list_type.to_string()), p_for->list);
		}
	}

	if (p_for->variable) {
		if (p_for->datatype_specifier) {
			GDScriptParser::DataType specified_type = type_from_metatype(resolve_datatype(p_for->datatype_specifier));
			if (!specified_type.is_variant()) {
				if (variable_type.is_variant() || !variable_type.is_hard_type()) {
					mark_node_unsafe(p_for->variable);
					p_for->use_conversion_assign = true;
				} else if (!is_type_compatible(specified_type, variable_type, true, p_for->variable)) {
					if (is_type_compatible(variable_type, specified_type)) {
						mark_node_unsafe(p_for->variable);
						p_for->use_conversion_assign = true;
					} else {
						push_error(vformat(R"(Unable to iterate on value of type "%s" with variable of type "%s".)", list_type.to_string(), specified_type.to_string()), p_for->datatype_specifier);
					}
				} else if (!is_type_compatible(specified_type, variable_type)) {
					p_for->use_conversion_assign = true;
				}
				if (p_for->list) {
					if (p_for->list->type == GDScriptParser::Node::ARRAY) {
						update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(p_for->list), specified_type);
					} else if (p_for->list->type == GDScriptParser::Node::DICTIONARY) {
						update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(p_for->list), specified_type, GDScriptParser::DataType::get_variant_type());
					}
				}
			}
			p_for->variable->set_datatype(specified_type);
		} else {
			p_for->variable->set_datatype(variable_type);
#ifdef DEBUG_ENABLED
			if (variable_type.is_hard_type()) {
				parser->push_warning(p_for->variable, GDScriptWarning::INFERRED_DECLARATION, R"("for" iterator variable)", p_for->variable->name);
			} else {
				parser->push_warning(p_for->variable, GDScriptWarning::UNTYPED_DECLARATION, R"("for" iterator variable)", p_for->variable->name);
			}
#endif // DEBUG_ENABLED
		}
	}

	HashMap<const GDScriptParser::Node *, GDScriptParser::DataType> previous_flow_narrowed_types = flow_narrowed_types;
	resolve_suite(p_for->loop);
	flow_narrowed_types = previous_flow_narrowed_types;
	p_for->set_datatype(p_for->loop->get_datatype());
#ifdef DEBUG_ENABLED
	if (p_for->variable) {
		is_shadowing(p_for->variable, R"("for" iterator variable)", true);
	}
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::resolve_while(GDScriptParser::WhileNode *p_while) {
	resolve_node(p_while->condition, false);

	HashMap<const GDScriptParser::Node *, GDScriptParser::DataType> previous_flow_narrowed_types = flow_narrowed_types;
	resolve_suite(p_while->loop);
	flow_narrowed_types = previous_flow_narrowed_types;
	p_while->set_datatype(p_while->loop->get_datatype());
}

void GDScriptAnalyzer::resolve_assert(GDScriptParser::AssertNode *p_assert) {
	reduce_expression(p_assert->condition);
	if (p_assert->message != nullptr) {
		reduce_expression(p_assert->message);
		if (!p_assert->message->get_datatype().has_no_type() && (p_assert->message->get_datatype().kind != GDScriptParser::DataType::BUILTIN || p_assert->message->get_datatype().builtin_type != Variant::STRING)) {
			push_error(R"(Expected string for assert error message.)", p_assert->message);
		}
	}

	p_assert->set_datatype(p_assert->condition->get_datatype());
	GDScriptParser::IdentifierNode *narrowed_identifier = nullptr;
	if (null_check_narrowing_identifier(p_assert->condition, true, narrowed_identifier)) {
		apply_flow_narrowing(narrowed_identifier);
	}

#ifdef DEBUG_ENABLED
	if (p_assert->condition->is_constant) {
		if (p_assert->condition->reduced_value.booleanize()) {
			parser->push_warning(p_assert->condition, GDScriptWarning::ASSERT_ALWAYS_TRUE);
		} else if (!(p_assert->condition->type == GDScriptParser::Node::LITERAL && static_cast<GDScriptParser::LiteralNode *>(p_assert->condition)->value.get_type() == Variant::BOOL)) {
			parser->push_warning(p_assert->condition, GDScriptWarning::ASSERT_ALWAYS_FALSE);
		}
	}
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::resolve_match(GDScriptParser::MatchNode *p_match) {
	reduce_expression(p_match->test);

	for (int i = 0; i < p_match->branches.size(); i++) {
		resolve_match_branch(p_match->branches[i], p_match->test);

		decide_suite_type(p_match, p_match->branches[i]);
	}

#ifdef DEBUG_ENABLED
	check_match_exhaustiveness(p_match);
#endif
}

#ifdef DEBUG_ENABLED
void GDScriptAnalyzer::check_match_exhaustiveness(GDScriptParser::MatchNode *p_match) {
	if (p_match->test == nullptr) {
		return; // Parse error: `match` with no test expression.
	}
	const GDScriptParser::DataType &match_type = p_match->test->get_datatype();
	if (!match_type.is_set()) {
		return; // Type unknown; cannot classify the domain.
	}

	// A branch counts as a default only with an unguarded wildcard/bind pattern.
	// The parser already clears `has_wildcard` when a guard is present.
	bool has_default = false;
	for (GDScriptParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->has_wildcard) {
			has_default = true;
			break;
		}
	}

	// Classify the matched type's domain.
	// `domain_values` maps each value's display name to its integer value.
	// Iteration order follows insertion order (Godot HashMap), i.e. enum
	// declaration order, so the unhandled list is deterministic.
	bool is_finite_domain = false;
	HashMap<StringName, int64_t> domain_values;
	String type_name;
	if (match_type.kind == GDScriptParser::DataType::ENUM) {
		is_finite_domain = true;
		domain_values = match_type.enum_values;
		type_name = match_type.enum_type;
	} else if (match_type.kind == GDScriptParser::DataType::BUILTIN && match_type.builtin_type == Variant::BOOL) {
		is_finite_domain = true;
		domain_values[SNAME("false")] = 0;
		domain_values[SNAME("true")] = 1;
		type_name = "bool";
	}

	if (!is_finite_domain) {
		if (!has_default) {
			parser->push_warning(p_match, GDScriptWarning::MATCH_WITHOUT_DEFAULT);
		}
		return;
	}

	if (has_default || domain_values.is_empty()) {
		return; // Exhaustive via default, or nothing to check.
	}

	// `match` compares typeof() before value, so only same-typed constants can
	// cover a value at runtime: INT for enums, BOOL for the bool domain.
	const Variant::Type expected_type = match_type.kind == GDScriptParser::DataType::ENUM ? Variant::INT : Variant::BOOL;

	// For nullable types, `null` (`Variant::NIL`) is a valid runtime value that
	// no enum integer or bool constant can cover, so it must be handled by an
	// explicit `null` pattern (or a wildcard) to be exhaustive.
	const bool domain_includes_null = match_type.is_nullable;

	// Collect values covered by unguarded, statically-constant patterns.
	bool null_covered = false;
	HashSet<int64_t> covered_values;
	for (GDScriptParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->guard_body != nullptr) {
			continue; // Guard may fail; does not guarantee coverage.
		}
		for (GDScriptParser::PatternNode *pattern : branch->patterns) {
			const GDScriptParser::ExpressionNode *value_node = nullptr;
			if (pattern->pattern_type == GDScriptParser::PatternNode::PT_LITERAL) {
				value_node = pattern->literal;
			} else if (pattern->pattern_type == GDScriptParser::PatternNode::PT_EXPRESSION) {
				value_node = pattern->expression;
			} else {
				// Array/dictionary patterns cannot cover enum integers or bool values.
				continue;
			}

			if (value_node == nullptr || !value_node->is_constant) {
				return; // Non-constant pattern: cannot prove coverage; bail out.
			}
			if (value_node->reduced_value.get_type() != expected_type) {
				// A `null` pattern covers the nullable domain's `null` value.
				if (domain_includes_null && value_node->reduced_value.get_type() == Variant::NIL) {
					null_covered = true;
				}
				// A different-typed constant can never match this domain at
				// runtime (match compares typeof() first), so it covers nothing.
				// Skip it — do NOT bail out, the value stays unhandled.
				continue;
			}
			covered_values.insert((int64_t)value_node->reduced_value);
		}
	}

	// Report any domain value with no covering pattern.
	Vector<String> unhandled;
	for (const KeyValue<StringName, int64_t> &E : domain_values) {
		if (!covered_values.has(E.value)) {
			unhandled.push_back(String(E.key));
		}
	}
	if (domain_includes_null && !null_covered) {
		unhandled.push_back("null");
	}

	if (!unhandled.is_empty()) {
		parser->push_warning(p_match, GDScriptWarning::NON_EXHAUSTIVE_MATCH, type_name, String(", ").join(unhandled));
	}
}
#endif // DEBUG_ENABLED

void GDScriptAnalyzer::resolve_match_branch(GDScriptParser::MatchBranchNode *p_match_branch, GDScriptParser::ExpressionNode *p_match_test) {
	// Apply annotations.
	for (GDScriptParser::AnnotationNode *&E : p_match_branch->annotations) {
		resolve_annotation(E);
		E->apply(parser, p_match_branch, nullptr); // TODO: Provide `p_class`.
	}

	for (int i = 0; i < p_match_branch->patterns.size(); i++) {
		resolve_match_pattern(p_match_branch->patterns[i], p_match_test);
	}

	if (p_match_branch->guard_body) {
		resolve_suite(p_match_branch->guard_body, false);
	}

	resolve_suite(p_match_branch->block);

	decide_suite_type(p_match_branch, p_match_branch->block);
}

void GDScriptAnalyzer::resolve_match_pattern(GDScriptParser::PatternNode *p_match_pattern, GDScriptParser::ExpressionNode *p_match_test, const GDScriptParser::DataType *p_match_test_type) {
	if (p_match_pattern == nullptr) {
		return;
	}

	GDScriptParser::DataType result;
	GDScriptParser::DataType match_test_type;
	bool has_match_test_type = false;
	if (p_match_test != nullptr) {
		match_test_type = p_match_test->get_datatype();
		has_match_test_type = match_test_type.is_set();
	} else if (p_match_test_type != nullptr) {
		match_test_type = *p_match_test_type;
		has_match_test_type = match_test_type.is_set();
	}

	switch (p_match_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_LITERAL:
			if (p_match_pattern->literal) {
				reduce_literal(p_match_pattern->literal);
				result = p_match_pattern->literal->get_datatype();
			}
			break;
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			if (p_match_pattern->expression) {
				GDScriptParser::ExpressionNode *expr = p_match_pattern->expression;
				reduce_expression(expr);
				result = expr->get_datatype();
				if (!expr->is_constant) {
					while (expr && expr->type == GDScriptParser::Node::SUBSCRIPT) {
						GDScriptParser::SubscriptNode *sub = static_cast<GDScriptParser::SubscriptNode *>(expr);
						if (!sub->is_attribute) {
							expr = nullptr;
						} else {
							expr = sub->base;
						}
					}
					if (!expr || expr->type != GDScriptParser::Node::IDENTIFIER) {
						push_error(R"(Expression in match pattern must be a constant expression, an identifier, or an attribute access ("A.B").)", expr);
					}
				}
			}
			break;
		case GDScriptParser::PatternNode::PT_BIND:
			if (has_match_test_type) {
				result = match_test_type;
			} else {
				result = GDScriptParser::DataType::get_variant_type();
			}
			p_match_pattern->bind->set_datatype(result);
#ifdef DEBUG_ENABLED
			is_shadowing(p_match_pattern->bind, "pattern bind", true);
			if (p_match_pattern->bind->usages == 0 && !String(p_match_pattern->bind->name).begins_with("_")) {
				parser->push_warning(p_match_pattern->bind, GDScriptWarning::UNUSED_VARIABLE, p_match_pattern->bind->name);
			}
#endif // DEBUG_ENABLED
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				GDScriptParser::DataType element_type;
				GDScriptParser::DataType *element_type_ptr = nullptr;
				if (has_match_test_type && match_test_type.kind == GDScriptParser::DataType::BUILTIN && match_test_type.builtin_type == Variant::ARRAY && match_test_type.has_container_element_type(0)) {
					element_type = match_test_type.get_container_element_type(0);
					element_type_ptr = &element_type;
				}
				resolve_match_pattern(p_match_pattern->array[i], nullptr, element_type_ptr);
				decide_suite_type(p_match_pattern, p_match_pattern->array[i]);
			}
			result = p_match_pattern->get_datatype();
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (int i = 0; i < p_match_pattern->dictionary.size(); i++) {
				if (p_match_pattern->dictionary[i].key) {
					reduce_expression(p_match_pattern->dictionary[i].key);
					if (!p_match_pattern->dictionary[i].key->is_constant) {
						push_error(R"(Expression in dictionary pattern key must be a constant.)", p_match_pattern->dictionary[i].key);
					}
				}

				if (p_match_pattern->dictionary[i].value_pattern) {
					GDScriptParser::DataType value_type;
					GDScriptParser::DataType *value_type_ptr = nullptr;
					if (has_match_test_type && match_test_type.kind == GDScriptParser::DataType::BUILTIN && match_test_type.builtin_type == Variant::DICTIONARY && match_test_type.has_container_element_type(1)) {
						value_type = match_test_type.get_container_element_type(1);
						value_type_ptr = &value_type;
					}
					resolve_match_pattern(p_match_pattern->dictionary[i].value_pattern, nullptr, value_type_ptr);
					decide_suite_type(p_match_pattern, p_match_pattern->dictionary[i].value_pattern);
				}
			}
			result = p_match_pattern->get_datatype();
			break;
		case GDScriptParser::PatternNode::PT_WILDCARD:
		case GDScriptParser::PatternNode::PT_REST:
			result.kind = GDScriptParser::DataType::VARIANT;
			break;
	}

	p_match_pattern->set_datatype(result);
}

void GDScriptAnalyzer::resolve_return(GDScriptParser::ReturnNode *p_return) {
	GDScriptParser::DataType result;

	GDScriptParser::DataType expected_type;
	bool has_expected_type = parser->current_function != nullptr;
	if (has_expected_type) {
		expected_type = parser->current_function->get_datatype();
	}

	if (p_return->return_value != nullptr) {
		bool is_void_function = has_expected_type && expected_type.is_hard_type() && expected_type.kind == GDScriptParser::DataType::BUILTIN && expected_type.builtin_type == Variant::NIL;
		bool is_call = p_return->return_value->type == GDScriptParser::Node::CALL;
		if (is_void_function && is_call) {
			// Pretend the call is a root expression to allow those that are "void".
			reduce_call(static_cast<GDScriptParser::CallNode *>(p_return->return_value), false, true);
		} else {
			reduce_expression(p_return->return_value);
		}
		if (is_void_function) {
			p_return->void_return = true;
			const GDScriptParser::DataType &return_type = p_return->return_value->datatype;
			if (is_call && !return_type.is_hard_type()) {
				String function_name = parser->current_function->identifier ? parser->current_function->identifier->name.operator String() : String("<anonymous function>");
				String called_function_name = static_cast<GDScriptParser::CallNode *>(p_return->return_value)->function_name.operator String();
#ifdef DEBUG_ENABLED
				parser->push_warning(p_return, GDScriptWarning::UNSAFE_VOID_RETURN, function_name, called_function_name);
#endif // DEBUG_ENABLED
				mark_node_unsafe(p_return);
			} else if (!is_call) {
				push_error("A void function cannot return a value.", p_return);
			}
			result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
			result.kind = GDScriptParser::DataType::BUILTIN;
			result.builtin_type = Variant::NIL;
			result.is_constant = true;
		} else {
			if (p_return->return_value->type == GDScriptParser::Node::ARRAY && has_expected_type && expected_type.has_container_element_type(0)) {
				update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(p_return->return_value), expected_type.get_container_element_type(0));
			} else if (p_return->return_value->type == GDScriptParser::Node::DICTIONARY && has_expected_type && expected_type.has_container_element_types()) {
				update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(p_return->return_value),
						expected_type.get_container_element_type_or_variant(0), expected_type.get_container_element_type_or_variant(1));
			}
			if (has_expected_type && expected_type.is_hard_type() && p_return->return_value->is_constant) {
				update_const_expression_builtin_type(p_return->return_value, expected_type, "return");
			}
			result = p_return->return_value->get_datatype();
		}
	} else {
		// Return type is null by default.
		result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::NIL;
		result.is_constant = true;
	}

	if (has_expected_type && !expected_type.is_variant()) {
		if (result.is_variant() || !result.is_hard_type()) {
			if (result.is_variant() && strict_dynamic_checks) {
				push_error(vformat(R"(Cannot return Variant value in strict dynamic mode; expected "%s".)",
								   expected_type.to_string()),
						p_return);
			} else {
				mark_node_unsafe(p_return);
			}
			if (!result.is_variant() && !is_type_compatible(expected_type, result, true, p_return)) {
				downgrade_node_type_source(p_return);
			}
		} else if (!is_type_compatible(expected_type, result, true, p_return)) {
			const bool nullable_mismatch = strict_null_checks && result.is_nullable && !expected_type.is_nullable && !expected_type.is_variant();
			mark_node_unsafe(p_return);
			if (nullable_mismatch || !is_type_compatible(result, expected_type)) {
				if (nullable_mismatch) {
					push_error(vformat(R"(Cannot return nullable value of type "%s"; expected non-nullable "%s".)",
									   result.to_string(),
									   expected_type.to_string()),
							p_return);
				} else {
					push_error(vformat(R"(Cannot return value of type "%s" because the function return type is "%s".)",
									   result.to_string(),
									   expected_type.to_string()),
							p_return);
				}
			}
#ifdef DEBUG_ENABLED
		} else if (expected_type.builtin_type == Variant::INT && result.builtin_type == Variant::FLOAT) {
			parser->push_warning(p_return, GDScriptWarning::NARROWING_CONVERSION);
#endif // DEBUG_ENABLED
		}
	}

	p_return->set_datatype(result);
}

void GDScriptAnalyzer::reduce_expression(GDScriptParser::ExpressionNode *p_expression, bool p_is_root) {
	// This one makes some magic happen.

	if (p_expression == nullptr) {
		return;
	}

	if (p_expression->reduced) {
		// Don't do this more than once.
		return;
	}

	p_expression->reduced = true;

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY:
			reduce_array(static_cast<GDScriptParser::ArrayNode *>(p_expression));
			break;
		case GDScriptParser::Node::ASSIGNMENT:
			reduce_assignment(static_cast<GDScriptParser::AssignmentNode *>(p_expression));
			break;
		case GDScriptParser::Node::AWAIT:
			reduce_await(static_cast<GDScriptParser::AwaitNode *>(p_expression));
			break;
		case GDScriptParser::Node::BINARY_OPERATOR:
			reduce_binary_op(static_cast<GDScriptParser::BinaryOpNode *>(p_expression));
			break;
		case GDScriptParser::Node::CALL:
			reduce_call(static_cast<GDScriptParser::CallNode *>(p_expression), false, p_is_root);
			break;
		case GDScriptParser::Node::CAST:
			reduce_cast(static_cast<GDScriptParser::CastNode *>(p_expression));
			break;
		case GDScriptParser::Node::DICTIONARY:
			reduce_dictionary(static_cast<GDScriptParser::DictionaryNode *>(p_expression));
			break;
		case GDScriptParser::Node::GET_NODE:
			reduce_get_node(static_cast<GDScriptParser::GetNodeNode *>(p_expression));
			break;
		case GDScriptParser::Node::IDENTIFIER:
			reduce_identifier(static_cast<GDScriptParser::IdentifierNode *>(p_expression));
			break;
		case GDScriptParser::Node::LAMBDA:
			reduce_lambda(static_cast<GDScriptParser::LambdaNode *>(p_expression));
			break;
		case GDScriptParser::Node::LITERAL:
			reduce_literal(static_cast<GDScriptParser::LiteralNode *>(p_expression));
			break;
		case GDScriptParser::Node::PRELOAD:
			reduce_preload(static_cast<GDScriptParser::PreloadNode *>(p_expression));
			break;
		case GDScriptParser::Node::SELF:
			reduce_self(static_cast<GDScriptParser::SelfNode *>(p_expression));
			break;
		case GDScriptParser::Node::SUBSCRIPT:
			reduce_subscript(static_cast<GDScriptParser::SubscriptNode *>(p_expression));
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR:
			reduce_ternary_op(static_cast<GDScriptParser::TernaryOpNode *>(p_expression), p_is_root);
			break;
		case GDScriptParser::Node::TYPE_TEST:
			reduce_type_test(static_cast<GDScriptParser::TypeTestNode *>(p_expression));
			break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			reduce_unary_op(static_cast<GDScriptParser::UnaryOpNode *>(p_expression));
			break;
		// Non-expressions. Here only to make sure new nodes aren't forgotten.
		case GDScriptParser::Node::NONE:
		case GDScriptParser::Node::ANNOTATION:
		case GDScriptParser::Node::ANNOTATION_DECLARATION:
		case GDScriptParser::Node::ASSERT:
		case GDScriptParser::Node::BREAK:
		case GDScriptParser::Node::BREAKPOINT:
		case GDScriptParser::Node::CLASS:
		case GDScriptParser::Node::CONSTANT:
		case GDScriptParser::Node::CONTINUE:
		case GDScriptParser::Node::ENUM:
		case GDScriptParser::Node::FOR:
		case GDScriptParser::Node::FUNCTION:
		case GDScriptParser::Node::IF:
		case GDScriptParser::Node::MATCH:
		case GDScriptParser::Node::MATCH_BRANCH:
		case GDScriptParser::Node::PARAMETER:
		case GDScriptParser::Node::PASS:
		case GDScriptParser::Node::PATTERN:
		case GDScriptParser::Node::RETURN:
		case GDScriptParser::Node::SIGNAL:
		case GDScriptParser::Node::SUITE:
		case GDScriptParser::Node::TYPE:
		case GDScriptParser::Node::TYPE_PARAMETER:
		case GDScriptParser::Node::VARIABLE:
		case GDScriptParser::Node::WHILE:
			ERR_FAIL_MSG("Reaching unreachable case");
	}

	if (p_expression->get_datatype().kind == GDScriptParser::DataType::UNRESOLVED) {
		// Prevent `is_type_compatible()` errors for incomplete expressions.
		// The error can still occur if `reduce_*()` is called directly.
		GDScriptParser::DataType dummy;
		dummy.kind = GDScriptParser::DataType::VARIANT;
		p_expression->set_datatype(dummy);
	}
}

void GDScriptAnalyzer::reduce_array(GDScriptParser::ArrayNode *p_array) {
	for (int i = 0; i < p_array->elements.size(); i++) {
		GDScriptParser::ExpressionNode *element = p_array->elements[i];
		reduce_expression(element);
	}

	// It's array in any case.
	GDScriptParser::DataType arr_type;
	arr_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	arr_type.kind = GDScriptParser::DataType::BUILTIN;
	arr_type.builtin_type = Variant::ARRAY;
	arr_type.is_constant = true;

	p_array->set_datatype(arr_type);
}

#ifdef DEBUG_ENABLED
static bool enum_has_value(const GDScriptParser::DataType p_type, int64_t p_value) {
	for (const KeyValue<StringName, int64_t> &E : p_type.enum_values) {
		if (E.value == p_value) {
			return true;
		}
	}
	return false;
}
#endif // DEBUG_ENABLED

void GDScriptAnalyzer::update_const_expression_builtin_type(GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_type, const char *p_usage, bool p_is_cast) {
	if (p_expression->get_datatype() == p_type) {
		return;
	}
	if (p_type.kind != GDScriptParser::DataType::BUILTIN && p_type.kind != GDScriptParser::DataType::ENUM) {
		return;
	}

	GDScriptParser::DataType expression_type = p_expression->get_datatype();
	bool is_enum_cast = p_is_cast && p_type.kind == GDScriptParser::DataType::ENUM && p_type.is_meta_type == false && expression_type.builtin_type == Variant::INT;
	if (!is_enum_cast && !is_type_compatible(p_type, expression_type, true, p_expression)) {
		push_error(vformat(R"(Cannot %s a value of type "%s" as "%s".)", p_usage, expression_type.to_string(), p_type.to_string()), p_expression);
		return;
	}

	if (p_type.is_nullable && p_expression->is_constant && p_expression->reduced_value.get_type() == Variant::NIL) {
		// An explicit null is kept as-is: a nullable target holds null without converting it to the underlying
		// type. Keyed on the reduced value so a null constant typed as Variant is handled too.
		p_expression->set_datatype(p_type);
		return;
	}

	GDScriptParser::DataType value_type = type_from_variant(p_expression->reduced_value, p_expression);
	if (expression_type.is_variant() && !is_enum_cast && !is_type_compatible(p_type, value_type, true, p_expression)) {
		push_error(vformat(R"(Cannot %s a value of type "%s" as "%s".)", p_usage, value_type.to_string(), p_type.to_string()), p_expression);
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_type.kind == GDScriptParser::DataType::ENUM && value_type.builtin_type == Variant::INT && !enum_has_value(p_type, p_expression->reduced_value)) {
		parser->push_warning(p_expression, GDScriptWarning::INT_AS_ENUM_WITHOUT_MATCH, p_usage, p_expression->reduced_value.stringify(), p_type.to_string());
	}
#endif // DEBUG_ENABLED

	if (value_type.builtin_type == p_type.builtin_type) {
		p_expression->set_datatype(p_type);
		return;
	}

	Variant converted_to;
	const Variant *converted_from = &p_expression->reduced_value;
	Callable::CallError call_error;
	Variant::construct(p_type.builtin_type, converted_to, &converted_from, 1, call_error);
	if (call_error.error) {
		push_error(vformat(R"(Failed to convert a value of type "%s" to "%s".)", value_type.to_string(), p_type.to_string()), p_expression);
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_type.builtin_type == Variant::INT && value_type.builtin_type == Variant::FLOAT) {
		parser->push_warning(p_expression, GDScriptWarning::NARROWING_CONVERSION);
	}
#endif // DEBUG_ENABLED

	p_expression->reduced_value = converted_to;
	p_expression->set_datatype(p_type);
}

// When an array literal is stored (or passed as function argument) to a typed context, we then assume the array is typed.
// This function determines which type is that (if any).
void GDScriptAnalyzer::update_array_literal_element_type(GDScriptParser::ArrayNode *p_array, const GDScriptParser::DataType &p_element_type) {
	GDScriptParser::DataType expected_type = p_element_type;

	for (int i = 0; i < p_array->elements.size(); i++) {
		GDScriptParser::ExpressionNode *element_node = p_array->elements[i];
		if (expected_type.kind == GDScriptParser::DataType::BUILTIN && expected_type.builtin_type == Variant::ARRAY && expected_type.has_container_element_type(0) && element_node->type == GDScriptParser::Node::ARRAY) {
			update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(element_node), expected_type.get_container_element_type(0));
		} else if (expected_type.kind == GDScriptParser::DataType::BUILTIN && expected_type.builtin_type == Variant::DICTIONARY && expected_type.has_container_element_types() && element_node->type == GDScriptParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(element_node),
					expected_type.get_container_element_type_or_variant(0), expected_type.get_container_element_type_or_variant(1));
		}
		if (element_node->is_constant) {
			update_const_expression_builtin_type(element_node, expected_type, "include");
		}
		const GDScriptParser::DataType &actual_type = element_node->get_datatype();
		if (actual_type.has_no_type()) {
			mark_node_unsafe(element_node);
			continue;
		}
		if (actual_type.is_variant()) {
			if (strict_dynamic_checks && !expected_type.is_variant()) {
				push_error(vformat(R"(Cannot include Variant value in array literal for "Array[%s]" in strict dynamic mode.)",
								   expected_type.to_string()),
						element_node);
				return;
			}
			mark_node_unsafe(element_node);
			continue;
		}
		if (!actual_type.is_hard_type()) {
			mark_node_unsafe(element_node);
			continue;
		}
		if (!is_type_compatible(expected_type, actual_type, true, p_array)) {
			if (is_type_compatible(actual_type, expected_type)) {
				mark_node_unsafe(element_node);
				continue;
			}
			push_error(vformat(R"(Cannot have an element of type "%s" in an array of type "Array[%s]".)", actual_type.to_string(), expected_type.to_string()), element_node);
			return;
		}
	}

	GDScriptParser::DataType array_type = p_array->get_datatype();
	array_type.set_container_element_type(0, expected_type);
	p_array->set_datatype(array_type);
}

// When a dictionary literal is stored (or passed as function argument) to a typed context, we then assume the dictionary is typed.
// This function determines which type is that (if any).
void GDScriptAnalyzer::update_dictionary_literal_element_type(GDScriptParser::DictionaryNode *p_dictionary, const GDScriptParser::DataType &p_key_element_type, const GDScriptParser::DataType &p_value_element_type) {
	GDScriptParser::DataType expected_key_type = p_key_element_type;
	GDScriptParser::DataType expected_value_type = p_value_element_type;

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		GDScriptParser::ExpressionNode *key_element_node = p_dictionary->elements[i].key;
		if (expected_key_type.kind == GDScriptParser::DataType::BUILTIN && expected_key_type.builtin_type == Variant::ARRAY && expected_key_type.has_container_element_type(0) && key_element_node->type == GDScriptParser::Node::ARRAY) {
			update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(key_element_node), expected_key_type.get_container_element_type(0));
		} else if (expected_key_type.kind == GDScriptParser::DataType::BUILTIN && expected_key_type.builtin_type == Variant::DICTIONARY && expected_key_type.has_container_element_types() && key_element_node->type == GDScriptParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(key_element_node),
					expected_key_type.get_container_element_type_or_variant(0), expected_key_type.get_container_element_type_or_variant(1));
		}
		if (key_element_node->is_constant) {
			update_const_expression_builtin_type(key_element_node, expected_key_type, "include");
		}
		const GDScriptParser::DataType &actual_key_type = key_element_node->get_datatype();
		if (actual_key_type.has_no_type()) {
			mark_node_unsafe(key_element_node);
		} else if (actual_key_type.is_variant()) {
			if (strict_dynamic_checks && !expected_key_type.is_variant()) {
				push_error(vformat("Cannot include Variant value as dictionary key for "
								   "\"Dictionary[%s, %s]\" in strict dynamic mode.",
								   expected_key_type.to_string(),
								   expected_value_type.to_string()),
						key_element_node);
				return;
			}
			mark_node_unsafe(key_element_node);
		} else if (!actual_key_type.is_hard_type()) {
			mark_node_unsafe(key_element_node);
		} else if (!is_type_compatible(expected_key_type, actual_key_type, true, p_dictionary)) {
			if (is_type_compatible(actual_key_type, expected_key_type)) {
				mark_node_unsafe(key_element_node);
			} else {
				push_error(vformat(R"(Cannot have a key of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_key_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), key_element_node);
				return;
			}
		}

		GDScriptParser::ExpressionNode *value_element_node = p_dictionary->elements[i].value;
		if (expected_value_type.kind == GDScriptParser::DataType::BUILTIN && expected_value_type.builtin_type == Variant::ARRAY && expected_value_type.has_container_element_type(0) && value_element_node->type == GDScriptParser::Node::ARRAY) {
			update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(value_element_node), expected_value_type.get_container_element_type(0));
		} else if (expected_value_type.kind == GDScriptParser::DataType::BUILTIN && expected_value_type.builtin_type == Variant::DICTIONARY && expected_value_type.has_container_element_types() && value_element_node->type == GDScriptParser::Node::DICTIONARY) {
			update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(value_element_node),
					expected_value_type.get_container_element_type_or_variant(0), expected_value_type.get_container_element_type_or_variant(1));
		}
		if (value_element_node->is_constant) {
			update_const_expression_builtin_type(value_element_node, expected_value_type, "include");
		}
		const GDScriptParser::DataType &actual_value_type = value_element_node->get_datatype();
		if (actual_value_type.has_no_type()) {
			mark_node_unsafe(value_element_node);
		} else if (actual_value_type.is_variant()) {
			if (strict_dynamic_checks && !expected_value_type.is_variant()) {
				push_error(vformat(R"(Cannot include Variant value as dictionary value for "Dictionary[%s, %s]" in strict dynamic mode.)",
								   expected_key_type.to_string(),
								   expected_value_type.to_string()),
						value_element_node);
				return;
			}
			mark_node_unsafe(value_element_node);
		} else if (!actual_value_type.is_hard_type()) {
			mark_node_unsafe(value_element_node);
		} else if (!is_type_compatible(expected_value_type, actual_value_type, true, p_dictionary)) {
			if (is_type_compatible(actual_value_type, expected_value_type)) {
				mark_node_unsafe(value_element_node);
			} else {
				push_error(vformat(R"(Cannot have a value of type "%s" in a dictionary of type "Dictionary[%s, %s]".)", actual_value_type.to_string(), expected_key_type.to_string(), expected_value_type.to_string()), value_element_node);
				return;
			}
		}
	}

	GDScriptParser::DataType dictionary_type = p_dictionary->get_datatype();
	dictionary_type.set_container_element_type(0, expected_key_type);
	dictionary_type.set_container_element_type(1, expected_value_type);
	p_dictionary->set_datatype(dictionary_type);
}

void GDScriptAnalyzer::reduce_assignment(GDScriptParser::AssignmentNode *p_assignment) {
	reduce_expression(p_assignment->assigned_value);

#ifdef DEBUG_ENABLED
	// Increment assignment count for local variables.
	// Before we reduce the assignee because we don't want to warn about not being assigned when performing the assignment.
	if (p_assignment->assignee->type == GDScriptParser::Node::IDENTIFIER) {
		GDScriptParser::IdentifierNode *id = static_cast<GDScriptParser::IdentifierNode *>(p_assignment->assignee);
		if (id->source == GDScriptParser::IdentifierNode::LOCAL_VARIABLE && id->variable_source) {
			id->variable_source->assignments++;
		}
	}
#endif // DEBUG_ENABLED

	clear_flow_narrowing(p_assignment->assignee);
	reduce_expression(p_assignment->assignee);

#ifdef DEBUG_ENABLED
	{
		bool is_subscript = false;
		GDScriptParser::ExpressionNode *base = p_assignment->assignee;
		while (base && base->type == GDScriptParser::Node::SUBSCRIPT) {
			is_subscript = true;
			base = static_cast<GDScriptParser::SubscriptNode *>(base)->base;
		}
		if (base && base->type == GDScriptParser::Node::IDENTIFIER) {
			GDScriptParser::IdentifierNode *id = static_cast<GDScriptParser::IdentifierNode *>(base);
			if (current_lambda && current_lambda->captures_indices.has(id->name)) {
				bool need_warn = false;
				if (is_subscript) {
					const GDScriptParser::DataType &id_type = id->datatype;
					if (id_type.is_hard_type()) {
						switch (id_type.kind) {
							case GDScriptParser::DataType::BUILTIN:
								// TODO: Change `Variant::is_type_shared()` to include packed arrays?
								need_warn = !Variant::is_type_shared(id_type.builtin_type) && id_type.builtin_type < Variant::PACKED_BYTE_ARRAY;
								break;
							case GDScriptParser::DataType::ENUM:
								need_warn = true;
								break;
							default:
								break;
						}
					}
				} else {
					need_warn = true;
				}
				if (need_warn) {
					parser->push_warning(p_assignment, GDScriptWarning::CONFUSABLE_CAPTURE_REASSIGNMENT, id->name);
				}
			}
		}
	}
#endif // DEBUG_ENABLED

	if (p_assignment->assigned_value == nullptr || p_assignment->assignee == nullptr) {
		return;
	}

	GDScriptParser::DataType assignee_type = p_assignment->assignee->get_datatype();

	if (assignee_type.is_constant) {
		push_error("Cannot assign a new value to a constant.", p_assignment->assignee);
		return;
	} else if (p_assignment->assignee->type == GDScriptParser::Node::SUBSCRIPT && static_cast<GDScriptParser::SubscriptNode *>(p_assignment->assignee)->base->is_constant) {
		const GDScriptParser::DataType &base_type = static_cast<GDScriptParser::SubscriptNode *>(p_assignment->assignee)->base->datatype;
		if (base_type.kind != GDScriptParser::DataType::SCRIPT && base_type.kind != GDScriptParser::DataType::CLASS) { // Static variables.
			push_error("Cannot assign a new value to a constant.", p_assignment->assignee);
			return;
		}
	} else if (assignee_type.is_read_only) {
		push_error("Cannot assign a new value to a read-only property.", p_assignment->assignee);
		return;
	} else if (p_assignment->assignee->type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *sub = static_cast<GDScriptParser::SubscriptNode *>(p_assignment->assignee);
		while (sub) {
			const GDScriptParser::DataType &base_type = sub->base->datatype;
			if (base_type.is_hard_type() && base_type.is_read_only) {
				if (base_type.kind == GDScriptParser::DataType::BUILTIN && !Variant::is_type_shared(base_type.builtin_type)) {
					push_error("Cannot assign a new value to a read-only property.", p_assignment->assignee);
					return;
				}
			} else {
				break;
			}
			if (sub->base->type == GDScriptParser::Node::SUBSCRIPT) {
				sub = static_cast<GDScriptParser::SubscriptNode *>(sub->base);
			} else {
				sub = nullptr;
			}
		}
	}

	// Check if assigned value is an array/dictionary literal, so we can make it a typed container too if appropriate.
	if (p_assignment->assigned_value->type == GDScriptParser::Node::ARRAY && assignee_type.is_hard_type() && assignee_type.has_container_element_type(0)) {
		update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(p_assignment->assigned_value), assignee_type.get_container_element_type(0));
	} else if (p_assignment->assigned_value->type == GDScriptParser::Node::DICTIONARY && assignee_type.is_hard_type() && assignee_type.has_container_element_types()) {
		update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(p_assignment->assigned_value),
				assignee_type.get_container_element_type_or_variant(0), assignee_type.get_container_element_type_or_variant(1));
	}

	if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE && assignee_type.is_hard_type() && p_assignment->assigned_value->is_constant) {
		update_const_expression_builtin_type(p_assignment->assigned_value, assignee_type, "assign");
	}

	GDScriptParser::DataType assigned_value_type = p_assignment->assigned_value->get_datatype();
	const String assignee_name = identifier_name_from_expression(p_assignment->assignee);

	if (p_assignment->assignee->type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_assignment->assignee);
		const GDScriptParser::DataType &base_type = subscript->base->get_datatype();
		if (base_type.kind == GDScriptParser::DataType::BUILTIN && base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types()) {
			if (subscript->index != nullptr && base_type.has_container_element_type(0)) {
				const GDScriptParser::DataType &key_type = base_type.get_container_element_type(0);
				const GDScriptParser::DataType &index_type = subscript->index->get_datatype();
				if (index_type.is_variant()) {
					mark_node_unsafe(subscript->index);
					if (strict_dynamic_checks && !key_type.is_variant()) {
						push_error(vformat(R"(Cannot use key of type "%s" in a dictionary of type "%s".)", index_type.to_string(), base_type.to_string()), subscript->index);
						return;
					}
				}
			}

			if (base_type.has_container_element_type(1) && assigned_value_type.is_variant()) {
				const GDScriptParser::DataType &value_type = base_type.get_container_element_type(1);
				mark_node_unsafe(p_assignment->assigned_value);
				if (strict_dynamic_checks && !value_type.is_variant()) {
					push_error(vformat(R"(Cannot assign a value of type "%s" to a dictionary value of type "%s".)", assigned_value_type.to_string(), value_type.to_string()), p_assignment->assigned_value);
					return;
				}
			}
		}
	}

	bool assignee_is_variant = assignee_type.is_variant();
	bool assignee_is_hard = assignee_type.is_hard_type();
	bool assigned_is_variant = assigned_value_type.is_variant();
	bool assigned_is_hard = assigned_value_type.is_hard_type();
	bool compatible = true;
	bool downgrades_assignee = false;
	bool downgrades_assigned = false;
	GDScriptParser::DataType op_type = assigned_value_type;
	if (p_assignment->operation != GDScriptParser::AssignmentNode::OP_NONE && !op_type.is_variant()) {
		op_type = get_operation_type(p_assignment->variant_op, assignee_type, assigned_value_type, compatible, p_assignment->assigned_value);

		if (assignee_is_variant) {
			// variant assignee
			mark_node_unsafe(p_assignment);
		} else if (!compatible) {
			// incompatible hard types and non-variant assignee
			mark_node_unsafe(p_assignment);
			if (assigned_is_variant) {
				// incompatible hard non-variant assignee and hard variant assigned
				p_assignment->use_conversion_assign = true;
			} else {
				// incompatible hard non-variant types
				push_error(vformat(R"(Invalid operands "%s" and "%s" for assignment operator.)", assignee_type.to_string(), assigned_value_type.to_string()), p_assignment);
			}
		} else if (op_type.type_source == GDScriptParser::DataType::UNDETECTED && !assigned_is_variant) {
			// incompatible non-variant types (at least one weak)
			downgrades_assignee = !assignee_is_hard;
			downgrades_assigned = !assigned_is_hard;
		}
	}
	p_assignment->set_datatype(op_type);

	if (assignee_is_variant) {
		if (!assignee_is_hard) {
			// weak variant assignee
			mark_node_unsafe(p_assignment);
		}
	} else {
		if (assignee_is_hard && !assigned_is_hard) {
			// hard non-variant assignee and weak assigned
			mark_node_unsafe(p_assignment);
			p_assignment->use_conversion_assign = true;
			downgrades_assigned = downgrades_assigned || (!assigned_is_variant && !is_type_compatible(assignee_type, op_type, true, p_assignment->assigned_value));
		} else if (compatible) {
			if (op_type.is_variant()) {
				// non-variant assignee and variant result
				mark_node_unsafe(p_assignment);
				if (strict_dynamic_checks) {
					if (!assignee_name.is_empty()) {
						push_error(vformat(R"(Cannot assign Variant value to variable "%s" in strict dynamic mode; expected "%s".)",
										   assignee_name,
										   assignee_type.to_string()),
								p_assignment->assigned_value);
					} else {
						push_error(vformat(R"(Cannot assign Variant value to target in strict dynamic mode; expected "%s".)",
										   assignee_type.to_string()),
								p_assignment->assigned_value);
					}
				} else if (assignee_is_hard) {
					// hard non-variant assignee and variant result
					p_assignment->use_conversion_assign = true;
				} else {
					// weak non-variant assignee and variant result
					downgrades_assignee = true;
				}
			} else if (!is_type_compatible(assignee_type, op_type, assignee_is_hard, p_assignment->assigned_value)) {
				// non-variant assignee and incompatible result
				mark_node_unsafe(p_assignment);
				if (assignee_is_hard) {
					const bool nullable_mismatch = strict_null_checks && op_type.is_nullable && !assignee_type.is_nullable && !assignee_type.is_variant();
					if (!nullable_mismatch && is_type_compatible(op_type, assignee_type)) {
						// hard non-variant assignee and maybe compatible result
						p_assignment->use_conversion_assign = true;
					} else {
						// hard non-variant assignee and incompatible result
						if (nullable_mismatch) {
							if (!assignee_name.is_empty()) {
								push_error(vformat(R"(Cannot assign nullable value of type "%s" to variable "%s"; expected non-nullable "%s".)",
												   assigned_value_type.to_string(),
												   assignee_name,
												   assignee_type.to_string()),
										p_assignment->assigned_value);
							} else {
								push_error(vformat(R"(Cannot assign nullable value of type "%s" to target; expected non-nullable "%s".)",
												   assigned_value_type.to_string(),
												   assignee_type.to_string()),
										p_assignment->assigned_value);
							}
						} else {
							push_error(vformat(R"(Value of type "%s" cannot be assigned to a variable of type "%s".)",
											   assigned_value_type.to_string(),
											   assignee_type.to_string()),
									p_assignment->assigned_value);
						}
					}
				} else {
					// weak non-variant assignee and incompatible result
					downgrades_assignee = true;
				}
			} else if ((assignee_type.has_container_element_type(0) && !op_type.has_container_element_type(0)) || (assignee_type.has_container_element_type(1) && !op_type.has_container_element_type(1))) {
				// Typed assignee and untyped result.
				mark_node_unsafe(p_assignment);
			}
		}
	}

	if (downgrades_assignee) {
		downgrade_node_type_source(p_assignment->assignee);
	}
	if (downgrades_assigned) {
		downgrade_node_type_source(p_assignment->assigned_value);
	}

#ifdef DEBUG_ENABLED
	if (assignee_type.is_hard_type() && assignee_type.builtin_type == Variant::INT && assigned_value_type.builtin_type == Variant::FLOAT) {
		parser->push_warning(p_assignment->assigned_value, GDScriptWarning::NARROWING_CONVERSION);
	}
	// Check for assignment with operation before assignment.
	if (p_assignment->operation != GDScriptParser::AssignmentNode::OP_NONE && p_assignment->assignee->type == GDScriptParser::Node::IDENTIFIER) {
		GDScriptParser::IdentifierNode *id = static_cast<GDScriptParser::IdentifierNode *>(p_assignment->assignee);
		// Use == 1 here because this assignment was already counted in the beginning of the function.
		if (id->source == GDScriptParser::IdentifierNode::LOCAL_VARIABLE && id->variable_source && id->variable_source->assignments == 1) {
			parser->push_warning(p_assignment, GDScriptWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, id->name, Variant::get_operator_name(p_assignment->variant_op));
		}
	}
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::reduce_await(GDScriptParser::AwaitNode *p_await) {
	if (p_await->to_await == nullptr) {
		GDScriptParser::DataType await_type;
		await_type.kind = GDScriptParser::DataType::VARIANT;
		p_await->set_datatype(await_type);
		return;
	}

	if (p_await->to_await->type == GDScriptParser::Node::CALL) {
		reduce_call(static_cast<GDScriptParser::CallNode *>(p_await->to_await), true);
	} else {
		reduce_expression(p_await->to_await);
	}

	GDScriptParser::DataType await_type = p_await->to_await->get_datatype();
	// We cannot infer the type of the result of waiting for a signal.
	if (await_type.is_hard_type() && await_type.kind == GDScriptParser::DataType::BUILTIN && await_type.builtin_type == Variant::SIGNAL) {
		await_type.kind = GDScriptParser::DataType::VARIANT;
		await_type.type_source = GDScriptParser::DataType::UNDETECTED;
	} else if (p_await->to_await->is_constant) {
		p_await->is_constant = p_await->to_await->is_constant;
		p_await->reduced_value = p_await->to_await->reduced_value;
	}
	await_type.is_coroutine = false;
	p_await->set_datatype(await_type);

#ifdef DEBUG_ENABLED
	GDScriptParser::DataType to_await_type = p_await->to_await->get_datatype();
	if (!to_await_type.is_coroutine && !to_await_type.is_variant() && to_await_type.builtin_type != Variant::SIGNAL) {
		parser->push_warning(p_await, GDScriptWarning::REDUNDANT_AWAIT);
	}
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::reduce_binary_op(GDScriptParser::BinaryOpNode *p_binary_op) {
	reduce_expression(p_binary_op->left_operand);
	reduce_expression(p_binary_op->right_operand);

	GDScriptParser::DataType left_type;
	if (p_binary_op->left_operand) {
		left_type = p_binary_op->left_operand->get_datatype();
	}
	GDScriptParser::DataType right_type;
	if (p_binary_op->right_operand) {
		right_type = p_binary_op->right_operand->get_datatype();
	}

	if (!left_type.is_set() || !right_type.is_set()) {
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_binary_op->variant_op == Variant::OP_DIVIDE &&
			(left_type.builtin_type == Variant::INT ||
					left_type.builtin_type == Variant::VECTOR2I ||
					left_type.builtin_type == Variant::VECTOR3I ||
					left_type.builtin_type == Variant::VECTOR4I) &&
			(right_type.builtin_type == Variant::INT ||
					right_type.builtin_type == left_type.builtin_type)) {
		parser->push_warning(p_binary_op, GDScriptWarning::INTEGER_DIVISION);
	}
#endif // DEBUG_ENABLED

	if (p_binary_op->left_operand->is_constant && p_binary_op->right_operand->is_constant) {
		p_binary_op->is_constant = true;
		if (p_binary_op->variant_op < Variant::OP_MAX) {
			bool valid = false;
			Variant::evaluate(p_binary_op->variant_op, p_binary_op->left_operand->reduced_value, p_binary_op->right_operand->reduced_value, p_binary_op->reduced_value, valid);
			if (!valid) {
				if (p_binary_op->reduced_value.get_type() == Variant::STRING) {
					push_error(vformat(R"(%s in operator %s.)", p_binary_op->reduced_value, Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
				} else {
					push_error(vformat(R"(Invalid operands to operator %s, %s and %s.)",
									   Variant::get_operator_name(p_binary_op->variant_op),
									   Variant::get_type_name(p_binary_op->left_operand->reduced_value.get_type()),
									   Variant::get_type_name(p_binary_op->right_operand->reduced_value.get_type())),
							p_binary_op);
				}
			}
		} else {
			ERR_PRINT("Parser bug: unknown binary operation.");
		}
		p_binary_op->set_datatype(type_from_variant(p_binary_op->reduced_value, p_binary_op));

		return;
	}

	GDScriptParser::DataType result;

	if ((p_binary_op->variant_op == Variant::OP_EQUAL || p_binary_op->variant_op == Variant::OP_NOT_EQUAL) &&
			((left_type.kind == GDScriptParser::DataType::BUILTIN && left_type.builtin_type == Variant::NIL) || (right_type.kind == GDScriptParser::DataType::BUILTIN && right_type.builtin_type == Variant::NIL))) {
		// "==" and "!=" operators always return a boolean when comparing to null.
		result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
	} else if (p_binary_op->variant_op == Variant::OP_MODULE && left_type.builtin_type == Variant::STRING) {
		// The modulo operator (%) on string acts as formatting and will always return a string.
		result.type_source = left_type.type_source;
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::STRING;
	} else if (left_type.is_variant() || right_type.is_variant()) {
		// Cannot infer type because one operand can be anything.
		result.kind = GDScriptParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for "%s" operator in strict dynamic mode.)*", Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
		} else {
			mark_node_unsafe(p_binary_op);
		}
	} else if (p_binary_op->variant_op < Variant::OP_MAX) {
		bool valid = false;
		result = get_operation_type(p_binary_op->variant_op, left_type, right_type, valid, p_binary_op);
		if (!valid) {
			push_error(vformat(R"(Invalid operands "%s" and "%s" for "%s" operator.)", left_type.to_string(), right_type.to_string(), Variant::get_operator_name(p_binary_op->variant_op)), p_binary_op);
		} else if (!result.is_hard_type()) {
			mark_node_unsafe(p_binary_op);
		}
	} else {
		ERR_PRINT("Parser bug: unknown binary operation.");
	}

	p_binary_op->set_datatype(result);
}

#ifdef SUGGEST_GODOT4_RENAMES
const char *get_rename_from_map(const char *map[][2], String key) {
	for (int index = 0; map[index][0]; index++) {
		if (map[index][0] == key) {
			return map[index][1];
		}
	}
	return nullptr;
}

// Checks if an identifier/function name has been renamed in Godot 4, uses ProjectConverter3To4 for rename map.
// Returns the new name if found, nullptr otherwise.
const char *check_for_renamed_identifier(String identifier, GDScriptParser::Node::Type type) {
	switch (type) {
		case GDScriptParser::Node::IDENTIFIER: {
			// Check properties
			const char *result = get_rename_from_map(RenamesMap3To4::gdscript_properties_renames, identifier);
			if (result) {
				return result;
			}
			// Check enum values
			result = get_rename_from_map(RenamesMap3To4::enum_renames, identifier);
			if (result) {
				return result;
			}
			// Check color constants
			result = get_rename_from_map(RenamesMap3To4::color_renames, identifier);
			if (result) {
				return result;
			}
			// Check type names
			result = get_rename_from_map(RenamesMap3To4::class_renames, identifier);
			if (result) {
				return result;
			}
			return get_rename_from_map(RenamesMap3To4::builtin_types_renames, identifier);
		}
		case GDScriptParser::Node::CALL: {
			const char *result = get_rename_from_map(RenamesMap3To4::gdscript_function_renames, identifier);
			if (result) {
				return result;
			}
			// Built-in Types are mistaken for function calls when the built-in type is not found.
			// Check built-in types if function rename not found
			return get_rename_from_map(RenamesMap3To4::builtin_types_renames, identifier);
		}
		// Signal references don't get parsed through the GDScriptAnalyzer. No support for signal rename hints.
		default:
			// No rename found, return null
			return nullptr;
	}
}
#endif // SUGGEST_GODOT4_RENAMES

void GDScriptAnalyzer::reduce_call(GDScriptParser::CallNode *p_call, bool p_is_await, bool p_is_root) {
	bool all_is_constant = true;
	HashMap<int, GDScriptParser::ArrayNode *> arrays; // For array literal to potentially type when passing.
	HashMap<int, GDScriptParser::DictionaryNode *> dictionaries; // Same, but for dictionaries.
	for (int i = 0; i < p_call->arguments.size(); i++) {
		reduce_expression(p_call->arguments[i]);
		if (p_call->arguments[i]->type == GDScriptParser::Node::ARRAY) {
			arrays[i] = static_cast<GDScriptParser::ArrayNode *>(p_call->arguments[i]);
		} else if (p_call->arguments[i]->type == GDScriptParser::Node::DICTIONARY) {
			dictionaries[i] = static_cast<GDScriptParser::DictionaryNode *>(p_call->arguments[i]);
		}
		all_is_constant = all_is_constant && p_call->arguments[i]->is_constant;
	}

	Finally clear_captured_flow_narrowing_after_call([&]() {
		clear_captured_flow_narrowing();
	});

	GDScriptParser::Node::Type callee_type = p_call->get_callee_type();
	GDScriptParser::DataType call_type;

	if (!p_call->is_super && callee_type == GDScriptParser::Node::IDENTIFIER) {
		// Call to name directly.
		StringName function_name = p_call->function_name;

#ifdef DEBUG_ENABLED
		const auto warn_return_value_discarded = [&]() {
			const GDScriptParser::DataType &return_type = p_call->get_datatype();
			if (p_is_root && return_type.kind != GDScriptParser::DataType::UNRESOLVED && return_type.builtin_type != Variant::NIL) {
				parser->push_warning(p_call, GDScriptWarning::RETURN_VALUE_DISCARDED, p_call->function_name);
			}
		};
#endif // DEBUG_ENABLED

		if (function_name == SNAME("Object")) {
			push_error(R"*(Invalid constructor "Object()", use "Object.new()" instead.)*", p_call);
			p_call->set_datatype(call_type);
			return;
		}

		Variant::Type builtin_type = GDScriptParser::get_builtin_type(function_name);
		// Builtin constructors and engine utility functions are not GDScript functions, so they
		// cannot accept named arguments. Reject them before these paths return.
		if (builtin_type < Variant::VARIANT_MAX || GDScriptUtilityFunctions::function_exists(function_name) || Variant::has_utility_function(function_name)) {
			reject_named_call_arguments(p_call);
		}
		if (builtin_type < Variant::VARIANT_MAX) {
			// Is a builtin constructor.
			call_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
			call_type.kind = GDScriptParser::DataType::BUILTIN;
			call_type.builtin_type = builtin_type;

			bool safe_to_fold = true;
			switch (builtin_type) {
				// Those are stored by reference so not suited for compile-time construction.
				// Because in this case they would be the same reference in all constructed values.
				case Variant::OBJECT:
				case Variant::DICTIONARY:
				case Variant::ARRAY:
				case Variant::PACKED_BYTE_ARRAY:
				case Variant::PACKED_INT32_ARRAY:
				case Variant::PACKED_INT64_ARRAY:
				case Variant::PACKED_FLOAT32_ARRAY:
				case Variant::PACKED_FLOAT64_ARRAY:
				case Variant::PACKED_STRING_ARRAY:
				case Variant::PACKED_VECTOR2_ARRAY:
				case Variant::PACKED_VECTOR3_ARRAY:
				case Variant::PACKED_COLOR_ARRAY:
				case Variant::PACKED_VECTOR4_ARRAY:
					safe_to_fold = false;
					break;
				default:
					break;
			}

			if (all_is_constant && safe_to_fold) {
				// Construct here.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Callable::CallError err;
				Variant value;
				Variant::construct(builtin_type, value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						push_error(vformat(R"*(Invalid argument for "%s()" constructor: argument %d should be "%s" but is "%s".)*", Variant::get_type_name(builtin_type), err.argument + 1,
										   Variant::get_type_name(Variant::Type(err.expected)), p_call->arguments[err.argument]->get_datatype().to_string()),
								p_call->arguments[err.argument]);
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD: {
						String signature = Variant::get_type_name(builtin_type) + "(";
						for (int i = 0; i < p_call->arguments.size(); i++) {
							if (i > 0) {
								signature += ", ";
							}
							signature += p_call->arguments[i]->get_datatype().to_string();
						}
						signature += ")";
						push_error(vformat(R"(No constructor of "%s" matches the signature "%s".)", Variant::get_type_name(builtin_type), signature), p_call->callee);
					} break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" constructor. Received %d but expected %d.)*", Variant::get_type_name(builtin_type), p_call->arguments.size(), err.expected), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" constructor. Received %d but expected %d.)*", Variant::get_type_name(builtin_type), p_call->arguments.size(), err.expected), p_call);
						break;
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				// If there's one argument, try to use copy constructor (those aren't explicitly defined).
				if (p_call->arguments.size() == 1) {
					GDScriptParser::DataType arg_type = p_call->arguments[0]->get_datatype();
					if (arg_type.is_hard_type() && !arg_type.is_variant()) {
						if (arg_type.kind == GDScriptParser::DataType::BUILTIN && arg_type.builtin_type == builtin_type) {
							// Okay.
							p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
							warn_return_value_discarded();
#endif // DEBUG_ENABLED
							return;
						}
					} else {
#ifdef DEBUG_ENABLED
						mark_node_unsafe(p_call);
						// Constructors support overloads.
						Vector<String> types;
						for (int i = 0; i < Variant::VARIANT_MAX; i++) {
							if (i != builtin_type && Variant::can_convert_strict((Variant::Type)i, builtin_type)) {
								types.push_back(Variant::get_type_name((Variant::Type)i));
							}
						}
						String expected_types = function_name;
						if (types.size() == 1) {
							expected_types += "\" or \"" + types[0];
						} else if (types.size() >= 2) {
							for (int i = 0; i < types.size() - 1; i++) {
								expected_types += "\", \"" + types[i];
							}
							expected_types += "\", or \"" + types[types.size() - 1];
						}
						parser->push_warning(p_call->arguments[0], GDScriptWarning::UNSAFE_CALL_ARGUMENT, "1", "constructor", function_name, expected_types, "Variant");
#endif // DEBUG_ENABLED
						p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
						warn_return_value_discarded();
#endif // DEBUG_ENABLED
						return;
					}
				}

				List<MethodInfo> constructors;
				Variant::get_constructor_list(builtin_type, &constructors);
				bool match = false;

				for (const MethodInfo &info : constructors) {
					if (p_call->arguments.size() < info.arguments.size() - info.default_arguments.size()) {
						continue;
					}
					if (p_call->arguments.size() > info.arguments.size()) {
						continue;
					}

					bool types_match = true;

					for (int64_t i = 0; i < p_call->arguments.size(); ++i) {
						GDScriptParser::DataType par_type = type_from_property(info.arguments[i], true);
						GDScriptParser::DataType arg_type = p_call->arguments[i]->get_datatype();
						if (!is_type_compatible(par_type, arg_type, true)) {
							types_match = false;
							break;
#ifdef DEBUG_ENABLED
						} else {
							if (par_type.builtin_type == Variant::INT && arg_type.builtin_type == Variant::FLOAT && builtin_type != Variant::INT) {
								parser->push_warning(p_call, GDScriptWarning::NARROWING_CONVERSION, function_name);
							}
#endif // DEBUG_ENABLED
						}
					}

					if (types_match) {
						for (int64_t i = 0; i < p_call->arguments.size(); ++i) {
							GDScriptParser::DataType par_type = type_from_property(info.arguments[i], true);
							if (p_call->arguments[i]->is_constant) {
								update_const_expression_builtin_type(p_call->arguments[i], par_type, "pass");
							}
#ifdef DEBUG_ENABLED
							if (!(par_type.is_variant() && par_type.is_hard_type())) {
								GDScriptParser::DataType arg_type = p_call->arguments[i]->get_datatype();
								if (arg_type.is_variant() || !arg_type.is_hard_type()) {
									mark_node_unsafe(p_call);
									parser->push_warning(p_call->arguments[i], GDScriptWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "constructor", function_name, par_type.to_string(), arg_type.to_string_strict());
								}
							}
#endif // DEBUG_ENABLED
						}
						match = true;
						call_type = type_from_property(info.return_val);
						break;
					}
				}

				if (!match) {
					String signature = Variant::get_type_name(builtin_type) + "(";
					for (int i = 0; i < p_call->arguments.size(); i++) {
						if (i > 0) {
							signature += ", ";
						}
						signature += p_call->arguments[i]->get_datatype().to_string();
					}
					signature += ")";
					push_error(vformat(R"(No constructor of "%s" matches the signature "%s".)", Variant::get_type_name(builtin_type), signature), p_call);
				}
			}

			if (builtin_type == Variant::CALLABLE && p_call->arguments.size() == 2) {
				GDScriptParser::DataType callable_type;
				if (callable_type_from_constant_method_args(p_call, 0, 1, callable_type)) {
					call_type = callable_type;
				} else {
					validate_strict_callable_method_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
				}
			}
			if (builtin_type == Variant::SIGNAL && p_call->arguments.size() == 2) {
				GDScriptParser::DataType signal_type;
				if (signal_type_from_receiver(p_call->arguments[0]->get_datatype(), p_call, 1, signal_type)) {
					call_type = signal_type;
				} else {
					validate_strict_signal_name_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
				}
			}

#ifdef DEBUG_ENABLED
			// Consider `Signal(self, "my_signal")` as an implicit use of the signal.
			if (builtin_type == Variant::SIGNAL && p_call->arguments.size() >= 2) {
				const GDScriptParser::ExpressionNode *object_arg = p_call->arguments[0];
				if (object_arg && object_arg->type == GDScriptParser::Node::SELF) {
					const GDScriptParser::ExpressionNode *signal_arg = p_call->arguments[1];
					if (signal_arg && signal_arg->is_constant) {
						const StringName &signal_name = signal_arg->reduced_value;
						if (parser->current_class->has_member(signal_name)) {
							const GDScriptParser::ClassNode::Member &member = parser->current_class->get_member(signal_name);
							if (member.type == GDScriptParser::ClassNode::Member::SIGNAL) {
								member.signal->usages++;
							}
						}
					}
				}
			}
#endif // DEBUG_ENABLED

			p_call->set_datatype(call_type);
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		} else if (GDScriptUtilityFunctions::function_exists(function_name)) {
			MethodInfo function_info = GDScriptUtilityFunctions::get_function_info(function_name);

			if (!p_is_root && !p_is_await && function_info.return_val.type == Variant::NIL && ((function_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0)) {
				push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", function_name), p_call);
			}

			if (all_is_constant && GDScriptUtilityFunctions::is_function_constant(function_name)) {
				// Can call on compilation.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Variant value;
				Callable::CallError err;
				GDScriptUtilityFunctions::get_function(function_name)(&value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						if (value.get_type() == Variant::STRING && !value.operator String().is_empty()) {
							push_error(vformat(R"*(Invalid argument for "%s()" function: %s)*", function_name, value), p_call->arguments[err.argument]);
						} else {
							// Do not use `type_from_property()` for expected type, since utility functions use their own checks.
							push_error(vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*", function_name, err.argument + 1,
											   Variant::get_type_name((Variant::Type)err.expected), p_call->arguments[err.argument]->get_datatype().to_string()),
									p_call->arguments[err.argument]);
						}
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD:
						push_error(vformat(R"(Invalid call for function "%s".)", function_name), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				validate_call_arg(function_info, p_call);
			}
			p_call->set_datatype(type_from_property(function_info.return_val));
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		} else if (Variant::has_utility_function(function_name)) {
			MethodInfo function_info = info_from_utility_func(function_name);

			if (!p_is_root && !p_is_await && function_info.return_val.type == Variant::NIL && ((function_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0)) {
				push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", function_name), p_call);
			}

			if (all_is_constant && Variant::get_utility_function_type(function_name) == Variant::UTILITY_FUNC_TYPE_MATH) {
				// Can call on compilation.
				Vector<const Variant *> args;
				for (int i = 0; i < p_call->arguments.size(); i++) {
					args.push_back(&(p_call->arguments[i]->reduced_value));
				}

				Variant value;
				Callable::CallError err;
				Variant::call_utility_function(function_name, &value, (const Variant **)args.ptr(), args.size(), err);

				switch (err.error) {
					case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
						if (value.get_type() == Variant::STRING && !value.operator String().is_empty()) {
							push_error(vformat(R"*(Invalid argument for "%s()" function: %s)*", function_name, value), p_call->arguments[err.argument]);
						} else {
							// Do not use `type_from_property()` for expected type, since utility functions use their own checks.
							push_error(vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*", function_name, err.argument + 1,
											   Variant::get_type_name((Variant::Type)err.expected), p_call->arguments[err.argument]->get_datatype().to_string()),
									p_call->arguments[err.argument]);
						}
						break;
					case Callable::CallError::CALL_ERROR_INVALID_METHOD:
						push_error(vformat(R"(Invalid call for function "%s".)", function_name), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
						push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
						push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", function_name, err.expected, p_call->arguments.size()), p_call);
						break;
					case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
					case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
						break; // Can't happen in a builtin constructor.
					case Callable::CallError::CALL_OK:
						p_call->is_constant = true;
						p_call->reduced_value = value;
						break;
				}
			} else {
				validate_call_arg(function_info, p_call);
			}
			p_call->is_noreturn = function_name == SNAME("push_fatal");
			p_call->set_datatype(type_from_property(function_info.return_val));
#ifdef DEBUG_ENABLED
			warn_return_value_discarded();
#endif // DEBUG_ENABLED
			return;
		}
	}

	GDScriptParser::DataType base_type;
	call_type.kind = GDScriptParser::DataType::VARIANT;
	bool is_self = false;

	if (p_call->is_super) {
		base_type = parser->current_class->base_type;
		base_type.is_meta_type = false;
		is_self = true;

		if (p_call->callee == nullptr && current_lambda != nullptr) {
			push_error("Cannot use `super()` inside a lambda.", p_call);
		}
	} else if (callee_type == GDScriptParser::Node::IDENTIFIER) {
		base_type = parser->current_class->get_datatype();
		base_type.is_meta_type = false;
		is_self = true;
	} else if (callee_type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_call->callee);
		if (subscript->base == nullptr) {
			// Invalid syntax, error already set on parser.
			p_call->set_datatype(call_type);
			mark_node_unsafe(p_call);
			return;
		}
		if (!subscript->is_attribute) {
			// A `name[...](...)` call. When `name` is a generic method in scope, the brackets are
			// an explicit type-argument list and the call is dispatched on `self`. Otherwise this
			// is a genuine call on an index expression and stays an error.
			GDScriptParser::FunctionNode *generic_method = nullptr;
			bool is_proxy_builtin = false;
			if (subscript->base->type == GDScriptParser::Node::IDENTIFIER) {
				GDScriptParser::IdentifierNode *base_identifier = static_cast<GDScriptParser::IdentifierNode *>(subscript->base);
				const StringName &base_name = base_identifier->name;
				// A local variable or parameter named like the method shadows it, so `name[...]()` is
				// an index call, not a generic application. The parser records the binding the name
				// resolved to at this position, so this stays correctly scoped (a local declared later
				// in the block does not shadow an earlier call).
				bool shadowed_by_local = false;
				switch (base_identifier->source) {
					case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
					case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
					case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
					case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
					case GDScriptParser::IdentifierNode::LOCAL_BIND:
						shadowed_by_local = true;
						break;
					default:
						break;
				}
				bool resolved_to_member = false;
				if (!shadowed_by_local) {
					generic_method = find_generic_method(parser->current_class, base_name, resolved_to_member);
				}
				// `create_proxy[T](handler)` is the built-in generic proxy constructor when it is not
				// shadowed by a local or a user-declared member of the same name.
				if (!shadowed_by_local && !resolved_to_member && generic_method == nullptr && base_name == SNAME("create_proxy")) {
					is_proxy_builtin = true;
				}
			}

			// A `receiver.method[...](...)` call applies an explicit type-argument list to a generic
			// method resolved on the receiver rather than self-dispatching. Only a generic method
			// (which only user GDScript classes declare) accepts the bracket list on an instance
			// receiver; a non-generic method keeps erroring like an index call.
			bool dispatched_on_receiver = false;
			if (!is_proxy_builtin && generic_method == nullptr && subscript->base->type == GDScriptParser::Node::SUBSCRIPT) {
				GDScriptParser::SubscriptNode *receiver_access = static_cast<GDScriptParser::SubscriptNode *>(subscript->base);
				if (receiver_access->is_attribute && receiver_access->attribute != nullptr && receiver_access->base != nullptr) {
					reduce_expression(receiver_access->base);
					GDScriptParser::DataType receiver_type = receiver_access->base->get_datatype();
					const StringName &method_name = receiver_access->attribute->name;
					bool receiver_found_member = false;
					generic_method = find_generic_method(receiver_type.class_type, method_name, receiver_found_member);
					if (generic_method != nullptr) {
						base_type = receiver_type;
						is_self = receiver_access->base->type == GDScriptParser::Node::SELF;
						if (p_call->function_name == StringName()) {
							p_call->function_name = method_name;
						}
						dispatched_on_receiver = true;
					}
				}
			}

			if (is_proxy_builtin) {
				reduce_call_create_proxy(p_call, subscript);
				return;
			}

			if (generic_method == nullptr) {
				push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*", p_call);
				p_call->set_datatype(call_type);
				mark_node_unsafe(p_call);
				return;
			}

			if (!dispatched_on_receiver) {
				base_type = parser->current_class->get_datatype();
				base_type.is_meta_type = false;
				is_self = true;
			}
		} else {
			if (subscript->attribute == nullptr) {
				// Invalid call. Error already sent in parser.
				p_call->set_datatype(call_type);
				mark_node_unsafe(p_call);
				return;
			}

			GDScriptParser::IdentifierNode *base_id = nullptr;
			if (subscript->base->type == GDScriptParser::Node::IDENTIFIER) {
				base_id = static_cast<GDScriptParser::IdentifierNode *>(subscript->base);
			}
			if (base_id && GDScriptParser::get_builtin_type(base_id->name) < Variant::VARIANT_MAX) {
				base_type = make_builtin_meta_type(GDScriptParser::get_builtin_type(base_id->name));
			} else {
				reduce_expression(subscript->base);
				base_type = subscript->base->get_datatype();
				is_self = subscript->base->type == GDScriptParser::Node::SELF;
			}
		}
	} else {
		// Invalid call. Error already sent in parser.
		// TODO: Could check if Callable here too.
		p_call->set_datatype(call_type);
		mark_node_unsafe(p_call);
		return;
	}

	int default_arg_count = 0;
	BitField<MethodFlags> method_flags = {};
	GDScriptParser::DataType return_type;
	List<GDScriptParser::DataType> par_types;
	bool is_noreturn = false;

	bool is_constructor = (base_type.is_meta_type || (p_call->callee && p_call->callee->type == GDScriptParser::Node::IDENTIFIER)) && p_call->function_name == SNAME("new");

	if (is_constructor) {
		if (base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type != nullptr && base_type.class_type->is_trait) {
			push_error(vformat(R"(Cannot construct trait "%s".)", _class_or_trait_name(base_type.class_type)), p_call);
			call_type.kind = GDScriptParser::DataType::VARIANT;
			call_type.type_source = GDScriptParser::DataType::INFERRED;
			p_call->set_datatype(call_type);
			return;
		}
		if (Engine::get_singleton()->has_singleton(base_type.native_type)) {
			push_error(vformat(R"(Cannot construct native class "%s" because it is an engine singleton.)", base_type.native_type), p_call);
			p_call->set_datatype(call_type);
			return;
		}
		if ((base_type.kind == GDScriptParser::DataType::CLASS && base_type.class_type->is_abstract) || (base_type.kind == GDScriptParser::DataType::SCRIPT && base_type.script_type.is_valid() && base_type.script_type->is_abstract())) {
			push_error(vformat(R"(Cannot construct abstract class "%s".)", base_type.to_string()), p_call);
		}
	}

	GDScriptParser::FunctionNode *found_function = nullptr;
	if (get_function_signature(p_call, is_constructor, base_type, p_call->function_name, return_type, par_types,
				default_arg_count, method_flags, nullptr, &is_noreturn, &found_function)) {
		p_call->is_static = method_flags.has_flag(METHOD_FLAG_STATIC);
		p_call->is_noreturn = is_noreturn;

		// Named arguments are only valid against a statically resolved GDScript function. When the
		// callee resolves to one, rewrite `name = value` arguments into canonical positional order
		// so the existing positional validation and codegen run unchanged; otherwise reject them.
		bool named_arguments_valid = true;
		if (found_function != nullptr) {
			named_arguments_valid = canonicalize_named_call_arguments(p_call, found_function);
			// Reordering may have changed argument positions, so rebuild the literal-typing maps.
			arrays.clear();
			dictionaries.clear();
			for (int i = 0; i < p_call->arguments.size(); i++) {
				if (p_call->arguments[i]->type == GDScriptParser::Node::ARRAY) {
					arrays[i] = static_cast<GDScriptParser::ArrayNode *>(p_call->arguments[i]);
				} else if (p_call->arguments[i]->type == GDScriptParser::Node::DICTIONARY) {
					dictionaries[i] = static_cast<GDScriptParser::DictionaryNode *>(p_call->arguments[i]);
				}
			}
		} else {
			reject_named_call_arguments(p_call);
		}

		// Generic methods solve their type parameters here, substituting the call's parameter
		// and return types before the arguments are validated against them.
		if (!is_constructor && found_function != nullptr && !found_function->type_parameters.is_empty()) {
			apply_generic_method_call(p_call, found_function, par_types, return_type);
		}
		// If the method is implemented in the class hierarchy, the virtual/abstract flag will not be set for that `MethodInfo` and the search stops there.
		// Virtual/abstract check only possible for super calls because class hierarchy is known. Objects may have scripts attached we don't know of at compile-time.
		if (p_call->is_super) {
			if (method_flags.has_flag(METHOD_FLAG_VIRTUAL)) {
				push_error(vformat(R"*(Cannot call the parent class' virtual function "%s()" because it hasn't been defined.)*", p_call->function_name), p_call);
			} else if (method_flags.has_flag(METHOD_FLAG_VIRTUAL_REQUIRED)) {
				push_error(vformat(R"*(Cannot call the parent class' abstract function "%s()" because it hasn't been defined.)*", p_call->function_name), p_call);
			}
		}

		// If the function requires typed arrays we must make literals be typed.
		for (const KeyValue<int, GDScriptParser::ArrayNode *> &E : arrays) {
			int index = E.key;
			if (index < par_types.size() && par_types.get(index).is_hard_type() && par_types.get(index).has_container_element_type(0)) {
				update_array_literal_element_type(E.value, par_types.get(index).get_container_element_type(0));
			}
		}
		for (const KeyValue<int, GDScriptParser::DictionaryNode *> &E : dictionaries) {
			int index = E.key;
			if (index < par_types.size() && par_types.get(index).is_hard_type() && par_types.get(index).has_container_element_types()) {
				GDScriptParser::DataType key = par_types.get(index).get_container_element_type_or_variant(0);
				GDScriptParser::DataType value = par_types.get(index).get_container_element_type_or_variant(1);
				update_dictionary_literal_element_type(E.value, key, value);
			}
		}
#ifdef TOOLS_ENABLED
		p_call->resolved_parameter_types.clear();
		for (const GDScriptParser::DataType &par_type : par_types) {
			p_call->resolved_parameter_types.push_back(par_type);
		}
#endif // TOOLS_ENABLED

		if (named_arguments_valid) {
			validate_call_arg(par_types, default_arg_count, method_flags.has_flag(METHOD_FLAG_VARARG), p_call, base_type.method_extra_allowed_argument_counts, base_type.method_unbound_argument_count);
		}
		validate_signal_connect_arg(base_type, p_call);
		validate_local_object_signal_callable_arg(p_call, is_self);
		validate_local_object_emit_signal_args(p_call, is_self);
		validate_typed_object_signal_api_args(base_type, p_call, is_self);

		if (base_type.kind == GDScriptParser::DataType::ENUM && base_type.is_meta_type) {
			// Enum type is treated as a dictionary value for function calls.
			base_type.is_meta_type = false;
		}

		if (is_self && static_context && !p_call->is_static) {
			// Get the parent function above any lambda.
			GDScriptParser::FunctionNode *parent_function = parser->current_function;
			while (parent_function && parent_function->source_lambda) {
				parent_function = parent_function->source_lambda->parent_function;
			}

			if (parent_function) {
				push_error(vformat(R"*(Cannot call non-static function "%s()" from the static function "%s()".)*", p_call->function_name, parent_function->identifier->name), p_call);
			} else {
				push_error(vformat(R"*(Cannot call non-static function "%s()" from a static variable initializer.)*", p_call->function_name), p_call);
			}
		} else if (!is_self && base_type.is_meta_type && !p_call->is_static) {
			base_type.is_meta_type = false; // For `to_string()`.
			push_error(vformat(R"*(Cannot call non-static function "%s()" on the class "%s" directly. Make an instance instead.)*", p_call->function_name, base_type.to_string()), p_call);
		} else if (is_self && !p_call->is_static) {
			mark_lambda_use_self();
		}

		if (!p_is_root && !p_is_await && return_type.is_hard_type() && return_type.kind == GDScriptParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
			push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", p_call->function_name), p_call);
		}

#ifdef DEBUG_ENABLED
		if (p_is_root && return_type.kind != GDScriptParser::DataType::UNRESOLVED && return_type.builtin_type != Variant::NIL &&
				!(p_call->is_super && p_call->function_name == GDScriptLanguage::get_singleton()->strings._init)) {
			parser->push_warning(p_call, GDScriptWarning::RETURN_VALUE_DISCARDED, p_call->function_name);
		}

		if (method_flags.has_flag(METHOD_FLAG_STATIC) && !is_constructor && !base_type.is_meta_type && !is_self) {
			String caller_type = base_type.to_string();

			parser->push_warning(p_call, GDScriptWarning::STATIC_CALLED_ON_INSTANCE, p_call->function_name, caller_type);
		}

		// Consider `emit_signal()`, `connect()`, `disconnect()`, and `is_connected()` as implicit uses of the signal.
		if (is_self && (p_call->function_name == SNAME("emit_signal") || p_call->function_name == SNAME("connect") || p_call->function_name == SNAME("disconnect") || p_call->function_name == SNAME("is_connected")) && !p_call->arguments.is_empty()) {
			const GDScriptParser::ExpressionNode *signal_arg = p_call->arguments[0];
			if (signal_arg && signal_arg->is_constant) {
				const StringName &signal_name = signal_arg->reduced_value;
				if (parser->current_class->has_member(signal_name)) {
					const GDScriptParser::ClassNode::Member &member = parser->current_class->get_member(signal_name);
					if (member.type == GDScriptParser::ClassNode::Member::SIGNAL) {
						member.signal->usages++;
					}
				}
			}
		}
#endif // DEBUG_ENABLED

		// Constructing a specialized generic class (`Box[int].new()`) yields a specialized instance,
		// so the call's result carries the reified type arguments supplied at the base.
		if (is_constructor && base_type.has_type_arguments()) {
			return_type.type_arguments = base_type.type_arguments;
		}

		call_type = return_type;
	} else {
		bool found = false;

		// Enums do not have functions other than the built-in dictionary ones.
		if (base_type.kind == GDScriptParser::DataType::ENUM && base_type.is_meta_type) {
			if (base_type.builtin_type == Variant::DICTIONARY) {
				push_error(vformat(R"*(Enums only have Dictionary built-in methods. Function "%s()" does not exist for enum "%s".)*", p_call->function_name, base_type.enum_type), p_call->callee);
			} else {
				push_error(vformat(R"*(The native enum "%s" does not behave like Dictionary and does not have methods of its own.)*", base_type.enum_type), p_call->callee);
			}
		} else if (!p_call->is_super && callee_type != GDScriptParser::Node::NONE) { // Check if the name exists as something else.
			GDScriptParser::IdentifierNode *callee_id;
			if (callee_type == GDScriptParser::Node::IDENTIFIER) {
				callee_id = static_cast<GDScriptParser::IdentifierNode *>(p_call->callee);
			} else {
				// Can only be attribute.
				callee_id = static_cast<GDScriptParser::SubscriptNode *>(p_call->callee)->attribute;
			}
			if (callee_id) {
				reduce_identifier_from_base(callee_id, &base_type);
				GDScriptParser::DataType callee_datatype = callee_id->get_datatype();
				if (callee_datatype.is_set() && !callee_datatype.is_variant()) {
					found = true;
					if (callee_datatype.builtin_type == Variant::CALLABLE) {
						push_error(vformat(R"*(Name "%s" is a Callable. You can call it with "%s.call()" instead.)*", p_call->function_name, p_call->function_name), p_call->callee);
					} else {
						push_error(vformat(R"*(Name "%s" called as a function but is a "%s".)*", p_call->function_name, callee_datatype.to_string()), p_call->callee);
					}
				} else if (!is_self && !(base_type.is_hard_type() && base_type.kind == GDScriptParser::DataType::BUILTIN)) {
					if (strict_dynamic_checks) {
						push_error(vformat(R"*(Cannot resolve method "%s" on type "%s" in strict dynamic mode.)*", p_call->function_name, base_type.to_string()), p_call->callee);
					} else {
#ifdef DEBUG_ENABLED
						parser->push_warning(p_call, GDScriptWarning::UNSAFE_METHOD_ACCESS, p_call->function_name, base_type.to_string());
						mark_node_unsafe(p_call);
#endif // DEBUG_ENABLED
					}
				}
			}
		}
		if (!found && (is_self || (base_type.is_hard_type() && base_type.kind == GDScriptParser::DataType::BUILTIN))) {
			String base_name = is_self && !p_call->is_super ? "self" : base_type.to_string();
#ifdef SUGGEST_GODOT4_RENAMES
			String rename_hint;
			if (GLOBAL_GET_CACHED(bool, "debug/gdscript/warnings/renamed_in_godot_4_hint")) {
				const char *renamed_function_name = check_for_renamed_identifier(p_call->function_name, p_call->type);
				if (renamed_function_name) {
					rename_hint = " " + vformat(R"(Did you mean to use "%s"?)", String(renamed_function_name) + "()");
				}
			}
			push_error(vformat(R"*(Function "%s()" not found in base %s.%s)*", p_call->function_name, base_name, rename_hint), p_call->is_super ? p_call : p_call->callee);
#else
			push_error(vformat(R"*(Function "%s()" not found in base %s.)*", p_call->function_name, base_name), p_call->is_super ? p_call : p_call->callee);
#endif // SUGGEST_GODOT4_RENAMES
		} else if (!found && (!p_call->is_super && base_type.is_hard_type() && base_type.is_meta_type)) {
			push_error(vformat(R"*(Static function "%s()" not found in base "%s".)*", p_call->function_name, base_type.to_string()), p_call);
		}
	}

	if (call_type.is_coroutine && !p_is_await) {
		if (p_is_root) {
#ifdef DEBUG_ENABLED
			parser->push_warning(p_call, GDScriptWarning::MISSING_AWAIT);
#endif // DEBUG_ENABLED
		} else {
			push_error(vformat(R"*(Function "%s()" is a coroutine, so it must be called with "await".)*", p_call->function_name), p_call);
		}
	}

	p_call->set_datatype(call_type);
}

void GDScriptAnalyzer::reduce_cast(GDScriptParser::CastNode *p_cast) {
	reduce_expression(p_cast->operand);

	GDScriptParser::DataType cast_type = type_from_metatype(resolve_datatype(p_cast->cast_type));

	if (!cast_type.is_set()) {
		mark_node_unsafe(p_cast);
		return;
	}

	p_cast->set_datatype(cast_type);
	if (p_cast->operand->is_constant) {
		update_const_expression_builtin_type(p_cast->operand, cast_type, "cast", true);
		if (cast_type.is_variant() || p_cast->operand->get_datatype() == cast_type) {
			p_cast->is_constant = true;
			p_cast->reduced_value = p_cast->operand->reduced_value;
		}
	}

	if (p_cast->operand->type == GDScriptParser::Node::ARRAY && cast_type.has_container_element_type(0)) {
		update_array_literal_element_type(static_cast<GDScriptParser::ArrayNode *>(p_cast->operand), cast_type.get_container_element_type(0));
	}

	if (p_cast->operand->type == GDScriptParser::Node::DICTIONARY && cast_type.has_container_element_types()) {
		update_dictionary_literal_element_type(static_cast<GDScriptParser::DictionaryNode *>(p_cast->operand),
				cast_type.get_container_element_type_or_variant(0), cast_type.get_container_element_type_or_variant(1));
	}

	if (!cast_type.is_variant()) {
		GDScriptParser::DataType op_type = p_cast->operand->get_datatype();
		if (op_type.is_variant() || !op_type.is_hard_type()) {
			mark_node_unsafe(p_cast);
#ifdef DEBUG_ENABLED
			parser->push_warning(p_cast, GDScriptWarning::UNSAFE_CAST, cast_type.to_string());
#endif // DEBUG_ENABLED
		} else {
			bool valid = false;
			if (op_type.builtin_type == Variant::INT && cast_type.kind == GDScriptParser::DataType::ENUM) {
				mark_node_unsafe(p_cast);
				valid = true;
			} else if (op_type.kind == GDScriptParser::DataType::ENUM && cast_type.builtin_type == Variant::INT) {
				valid = true;
			} else if (op_type.kind == GDScriptParser::DataType::BUILTIN && cast_type.kind == GDScriptParser::DataType::BUILTIN) {
				valid = Variant::can_convert(op_type.builtin_type, cast_type.builtin_type);
			} else if (op_type.kind != GDScriptParser::DataType::BUILTIN && cast_type.kind != GDScriptParser::DataType::BUILTIN) {
				valid = is_type_compatible(cast_type, op_type) || is_type_compatible(op_type, cast_type);
			}

			if (!valid) {
				push_error(vformat(R"(Invalid cast. Cannot convert from "%s" to "%s".)", op_type.to_string(), cast_type.to_string()), p_cast->cast_type);
			}
		}
	}
}

void GDScriptAnalyzer::reduce_dictionary(GDScriptParser::DictionaryNode *p_dictionary) {
	HashMap<Variant, GDScriptParser::ExpressionNode *, HashMapHasherDefault, StringLikeVariantComparator> elements;

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		const GDScriptParser::DictionaryNode::Pair &element = p_dictionary->elements[i];
		if (p_dictionary->style == GDScriptParser::DictionaryNode::PYTHON_DICT) {
			reduce_expression(element.key);
		}
		reduce_expression(element.value);

		if (element.key->is_constant) {
			if (elements.has(element.key->reduced_value)) {
				push_error(vformat(R"(Key "%s" was already used in this dictionary (at line %d).)", element.key->reduced_value, elements[element.key->reduced_value]->start_line), element.key);
			} else {
				elements[element.key->reduced_value] = element.value;
			}
		}
	}

	// It's dictionary in any case.
	GDScriptParser::DataType dict_type;
	dict_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	dict_type.kind = GDScriptParser::DataType::BUILTIN;
	dict_type.builtin_type = Variant::DICTIONARY;
	dict_type.is_constant = true;

	p_dictionary->set_datatype(dict_type);
}

void GDScriptAnalyzer::reduce_get_node(GDScriptParser::GetNodeNode *p_get_node) {
	GDScriptParser::DataType result;
	result.kind = GDScriptParser::DataType::VARIANT;

	if (!ClassDB::is_parent_class(parser->current_class->base_type.native_type, SNAME("Node"))) {
		push_error(vformat(R"*(Cannot use shorthand "get_node()" notation ("%c") on a class that isn't a node.)*", p_get_node->use_dollar ? '$' : '%'), p_get_node);
		p_get_node->set_datatype(result);
		return;
	}

	if (static_context) {
		push_error(vformat(R"*(Cannot use shorthand "get_node()" notation ("%c") in a static function.)*", p_get_node->use_dollar ? '$' : '%'), p_get_node);
		p_get_node->set_datatype(result);
		return;
	}

	mark_lambda_use_self();

	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = GDScriptParser::DataType::NATIVE;
	result.builtin_type = Variant::OBJECT;
	result.native_type = SNAME("Node");
	p_get_node->set_datatype(result);
}

GDScriptParser::DataType GDScriptAnalyzer::make_global_class_meta_type(const StringName &p_class_name, const GDScriptParser::Node *p_source) {
	GDScriptParser::DataType type;

	String path = ScriptServer::get_global_class_path(p_class_name);
	String ext = path.get_extension();
	if (ext == GDScriptLanguage::get_singleton()->get_extension()) {
		Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(path);
		if (ref.is_null()) {
			push_error(vformat(R"(Could not find script for class "%s".)", p_class_name), p_source);
			type.type_source = GDScriptParser::DataType::UNDETECTED;
			type.kind = GDScriptParser::DataType::VARIANT;
			return type;
		}

		Error err = ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
		if (err) {
			push_error(vformat(R"(Could not resolve class "%s", because of a parser error.)", p_class_name), p_source);
			type.type_source = GDScriptParser::DataType::UNDETECTED;
			type.kind = GDScriptParser::DataType::VARIANT;
			return type;
		}

		return ref->get_parser()->head->get_datatype();
	} else {
		return make_script_meta_type(ResourceLoader::load(path, "Script"));
	}
}

static String _join_identifier_chain(const Vector<GDScriptParser::IdentifierNode *> &p_chain, int p_from, int p_count) {
	String result;
	for (int i = p_from; i < p_count; i++) {
		if (i > p_from) {
			result += ".";
		}
		result += String(p_chain[i]->name);
	}
	return result;
}

static bool _string_vector_has(const LocalVector<String> &p_strings, const String &p_string) {
	for (const String &string : p_strings) {
		if (string == p_string) {
			return true;
		}
	}
	return false;
}

static bool _namespace_exists_in_global_classes(const LocalVector<StringName> &p_global_classes, const String &p_namespace) {
	if (p_namespace.is_empty()) {
		return false;
	}

	const String namespace_prefix = p_namespace + ".";
	for (const StringName &global_class : p_global_classes) {
		if (String(global_class).begins_with(namespace_prefix)) {
			return true;
		}
	}

	return false;
}

#ifdef DEBUG_ENABLED
static String _get_namespace_warning_name(const String &p_namespace) {
	return p_namespace.is_empty() ? String("<global>") : p_namespace;
}

struct MixedNamespaceDirectoryClass {
	String script_path;
	String namespace_name;
};

struct MixedNamespaceDirectoryCache {
	uint64_t global_class_cache_version = uint64_t(-1);
	HashMap<String, Vector<MixedNamespaceDirectoryClass>> classes_by_directory;
};

static thread_local MixedNamespaceDirectoryCache mixed_namespace_directory_cache;

static String _format_namespace_warning_list(Vector<String> p_namespaces) {
	p_namespaces.sort();

	String result;
	for (int i = 0; i < p_namespaces.size(); i++) {
		if (i > 0) {
			result += i == p_namespaces.size() - 1 ? (p_namespaces.size() == 2 ? " and " : ", and ") : ", ";
		}
		result += "\"" + p_namespaces[i] + "\"";
	}
	return result;
}

static void _update_mixed_namespace_directory_cache() {
	const uint64_t global_class_cache_version = ScriptServer::get_global_class_cache_version();
	if (mixed_namespace_directory_cache.global_class_cache_version == global_class_cache_version) {
		return;
	}

	mixed_namespace_directory_cache.global_class_cache_version = global_class_cache_version;
	mixed_namespace_directory_cache.classes_by_directory.clear();

	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);

	for (const StringName &global_class : global_classes) {
		const String class_path = GDScript::canonicalize_path(ScriptServer::get_global_class_path(global_class));
		if (class_path.is_empty()) {
			continue;
		}

		const String class_dir = class_path.get_base_dir();
		if (class_dir.is_empty()) {
			continue;
		}

		String class_namespace;
		ScriptServer::get_global_class_name_parts(global_class, nullptr, &class_namespace);

		MixedNamespaceDirectoryClass class_info;
		class_info.script_path = class_path;
		class_info.namespace_name = _get_namespace_warning_name(class_namespace);
		mixed_namespace_directory_cache.classes_by_directory[class_dir].push_back(class_info);
	}
}
#endif // DEBUG_ENABLED

bool GDScriptAnalyzer::get_global_class_in_namespace(const String &p_namespace, const StringName &p_class_name, StringName &r_global_class_name) const {
	if (p_namespace.is_empty()) {
		return false;
	}

	const String global_class_name = p_namespace + "." + String(p_class_name);
	if (!ScriptServer::is_global_class(global_class_name)) {
		return false;
	}

	r_global_class_name = global_class_name;
	return true;
}

bool GDScriptAnalyzer::get_imported_global_class(const StringName &p_class_name, const GDScriptParser::Node *p_source, StringName &r_global_class_name, bool &r_error, const String &p_symbol_kind) {
	r_error = false;
	String matched_import;
	LocalVector<String> checked_imports;

	for (const String &import : parser->head->imports) {
		if (_string_vector_has(checked_imports, import)) {
			continue;
		}
		checked_imports.push_back(import);

		StringName candidate;
		if (!get_global_class_in_namespace(import, p_class_name, candidate)) {
			continue;
		}

		if (r_global_class_name != StringName() && r_global_class_name != candidate) {
			push_error(vformat(R"(Could not resolve %s "%s": imported namespaces "%s" and "%s" are ambiguous.)", p_symbol_kind, p_class_name, matched_import, import), p_source);
			r_error = true;
			return true;
		}

		r_global_class_name = candidate;
		matched_import = import;
	}

	return r_global_class_name != StringName();
}

bool GDScriptAnalyzer::get_namespace_global_class_from_type_chain(const Vector<GDScriptParser::IdentifierNode *> &p_type_chain, const GDScriptParser::Node *p_source, StringName &r_global_class_name, int &r_type_chain_size, bool &r_error, const String &p_symbol_kind) {
	r_error = false;
	r_type_chain_size = 0;
	if (p_type_chain.is_empty()) {
		return false;
	}

	for (int prefix_size = p_type_chain.size(); prefix_size >= 1; prefix_size--) {
		const String class_prefix = _join_identifier_chain(p_type_chain, 0, prefix_size);

		if (prefix_size > 1 && ScriptServer::is_global_class(class_prefix)) {
			r_global_class_name = class_prefix;
			r_type_chain_size = prefix_size;
			return true;
		}

		StringName namespace_candidate;
		if (get_global_class_in_namespace(parser->head->namespace_name, class_prefix, namespace_candidate)) {
			r_global_class_name = namespace_candidate;
			r_type_chain_size = prefix_size;
			return true;
		}

		StringName imported_candidate;
		bool import_error = false;
		if (get_imported_global_class(class_prefix, p_source, imported_candidate, import_error, p_symbol_kind)) {
			if (import_error) {
				r_error = true;
				return true;
			}

			r_global_class_name = imported_candidate;
			r_type_chain_size = prefix_size;
			return true;
		}
	}

	return false;
}

bool GDScriptAnalyzer::is_namespace_chain_root_shadowed(GDScriptParser::IdentifierNode *p_identifier) {
	// Keep this in sync with the non-global lookup precedence in reduce_identifier().
	const StringName &name = p_identifier->name;

	if (p_identifier->suite && p_identifier->suite->has_local(name)) {
		return true;
	}

	if (GDScriptParser::get_builtin_type(name) < Variant::VARIANT_MAX || class_exists(name)) {
		return true;
	}

	List<GDScriptParser::ClassNode *> script_classes;
	get_class_node_current_scope_classes(parser->current_class, &script_classes, p_identifier);
	for (GDScriptParser::ClassNode *script_class : script_classes) {
		if ((script_class->identifier && script_class->identifier->name == name) || script_class->members_indices.has(name)) {
			return true;
		}
	}

	const StringName native = parser->current_class->base_type.native_type;
	if (!class_exists(native)) {
		return false;
	}

	if (ClassDB::has_property(native, name)) {
		return true;
	}

	MethodInfo method_info;
	if (ClassDB::get_method_info(native, name, &method_info) || ClassDB::get_signal(native, name, &method_info)) {
		return true;
	}

	if (ClassDB::has_enum(native, name)) {
		return true;
	}

	bool valid = false;
	ClassDB::get_integer_constant(native, name, &valid);
	return valid;
}

static const GDScriptParser::Node *_trait_use_source(const GDScriptParser::ClassNode::TraitUse &p_trait_use,
		const GDScriptParser::ClassNode *p_owner) {
	if (!p_trait_use.name.is_empty()) {
		return p_trait_use.name[0];
	}
	return p_owner;
}

static void _append_trait_unique(Vector<GDScriptParser::ClassNode *> &r_traits, GDScriptParser::ClassNode *p_trait) {
	if (p_trait != nullptr && !r_traits.has(p_trait)) {
		r_traits.push_back(p_trait);
	}
}

static const GDScriptParser::Node *_trait_requirement_source(const GDScriptParser::ClassNode *p_class,
		GDScriptParser::ClassNode *p_trait) {
	for (const GDScriptParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		if (trait_use.resolved_trait == p_trait) {
			return _trait_use_source(trait_use, p_class);
		}
		if (trait_use.resolved_trait != nullptr && trait_use.resolved_trait->resolved_traits.has(p_trait)) {
			return _trait_use_source(trait_use, p_class);
		}
	}
	return p_class->identifier != nullptr ? static_cast<const GDScriptParser::Node *>(p_class->identifier) : p_class;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::resolve_nested_trait_reference(GDScriptParser::ClassNode *p_base,
		const GDScriptParser::ClassNode::TraitUse &p_trait_use, int p_chain_index,
		const GDScriptParser::Node *p_source) {
	GDScriptParser::ClassNode *current = p_base;
	for (int i = p_chain_index; i < p_trait_use.name.size(); i++) {
		const StringName &name = p_trait_use.name[i]->name;
		if (current == nullptr || !current->members_indices.has(name)) {
			push_error(vformat(R"(Could not resolve trait "%s".)", p_trait_use.to_string()), p_source);
			return nullptr;
		}

		resolve_class_member(current, name, p_source);
		GDScriptParser::ClassNode::Member member = current->get_member(name);
		if (member.type != GDScriptParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
			push_error(vformat(R"(Cannot use %s "%s" as a trait.)", member.get_type_name(), name), p_trait_use.name[i]);
			return nullptr;
		}
		current = member.m_class;
	}

	return current;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::resolve_local_trait_reference(GDScriptParser::ClassNode *p_owner,
		const GDScriptParser::ClassNode::TraitUse &p_trait_use, const GDScriptParser::Node *p_source,
		bool &r_found) {
	r_found = false;
	if (p_trait_use.name.is_empty()) {
		return nullptr;
	}

	const StringName &first = p_trait_use.name[0]->name;
	List<GDScriptParser::ClassNode *> script_classes;
	get_class_node_current_scope_classes(p_owner, &script_classes, p_trait_use.name[0]);
	for (GDScriptParser::ClassNode *script_class : script_classes) {
		GDScriptParser::ClassNode *candidate = nullptr;

		if (script_class->identifier != nullptr && script_class->identifier->name == first) {
			candidate = script_class;
		} else if (script_class->members_indices.has(first)) {
			resolve_class_member(script_class, first, p_source);
			GDScriptParser::ClassNode::Member member = script_class->get_member(first);
			if (member.type != GDScriptParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
				push_error(vformat(R"(Cannot use %s "%s" as a trait.)", member.get_type_name(), first),
						p_trait_use.name[0]);
				r_found = true;
				return nullptr;
			}
			candidate = member.m_class;
		}

		if (candidate == nullptr) {
			continue;
		}

		r_found = true;
		candidate = resolve_nested_trait_reference(candidate, p_trait_use, 1, p_source);
		if (candidate == nullptr) {
			return nullptr;
		}
		if (!candidate->is_trait) {
			push_error(vformat(R"(Class "%s" cannot be used as a trait.)", _class_or_trait_name(candidate)),
					p_trait_use.name[0]);
			return nullptr;
		}
		return candidate;
	}

	return nullptr;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::resolve_global_trait_reference(const StringName &p_global_class_name,
		const GDScriptParser::Node *p_source) {
	if (!ScriptServer::is_global_class(p_global_class_name)) {
		return nullptr;
	}

	const String path = ScriptServer::get_global_class_path(p_global_class_name);
	GDScriptParser::ClassNode *global_class = nullptr;
	if (GDScript::is_canonically_equal_paths(path, parser->script_path)) {
		global_class = parser->head;
	} else {
		Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(path);
		if (ref.is_null()) {
			push_error(vformat(R"(Could not parse global trait "%s" from "%s".)", p_global_class_name, path), p_source);
			return nullptr;
		}
		Error err = ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
		if (err != OK) {
			push_error(vformat(R"(Could not resolve inheritance for global trait "%s" from "%s".)",
							   p_global_class_name, path),
					p_source);
			return nullptr;
		}
		global_class = ref->get_parser()->head;
	}

	if (global_class == nullptr) {
		return nullptr;
	}
	if (!global_class->is_trait) {
		push_error(vformat(R"(Class "%s" cannot be used as a trait.)", p_global_class_name), p_source);
		return nullptr;
	}
	return global_class;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::resolve_trait_reference(GDScriptParser::ClassNode *p_owner,
		GDScriptParser::ClassNode::TraitUse &r_trait_use, const GDScriptParser::Node *p_source) {
	if (r_trait_use.resolved_trait != nullptr) {
		return r_trait_use.resolved_trait;
	}

	bool found_local = false;
	GDScriptParser::ClassNode *trait = resolve_local_trait_reference(p_owner, r_trait_use, p_source, found_local);
	if (found_local) {
		r_trait_use.resolved_trait = trait;
		return trait;
	}

	StringName namespace_global_class;
	bool namespace_error = false;
	int namespace_type_chain_size = 0;
	if (get_namespace_global_class_from_type_chain(r_trait_use.name, p_source, namespace_global_class,
				namespace_type_chain_size, namespace_error, "trait")) {
		if (namespace_error) {
			return nullptr;
		}
		trait = resolve_global_trait_reference(namespace_global_class, p_source);
		if (trait != nullptr) {
			trait = resolve_nested_trait_reference(trait, r_trait_use, namespace_type_chain_size, p_source);
		}
		if (trait != nullptr && !trait->is_trait) {
			push_error(vformat(R"(Class "%s" cannot be used as a trait.)", _class_or_trait_name(trait)), p_source);
			return nullptr;
		}
		r_trait_use.resolved_trait = trait;
		return trait;
	}

	if (r_trait_use.name.size() == 1) {
		const StringName &name = r_trait_use.name[0]->name;
		if (ScriptServer::is_global_class(name)) {
			trait = resolve_global_trait_reference(name, p_source);
			r_trait_use.resolved_trait = trait;
			return trait;
		}
	}

	push_error(vformat(R"(Could not resolve trait "%s".)", r_trait_use.to_string()), p_source);
	return nullptr;
}

bool GDScriptAnalyzer::datatype_derives_from_datatype(GDScriptParser::DataType p_type,
		const GDScriptParser::DataType &p_base) {
	if (!p_type.is_set() || !p_base.is_set()) {
		return false;
	}

	if (is_type_compatible(p_base, p_type)) {
		return true;
	}

	while (p_type.is_set()) {
		if (p_base.kind == GDScriptParser::DataType::NATIVE && p_type.kind == GDScriptParser::DataType::NATIVE) {
			return p_type.native_type == p_base.native_type || ClassDB::is_parent_class(p_type.native_type, p_base.native_type);
		}

		if (p_base.kind == GDScriptParser::DataType::CLASS &&
				p_type.kind == GDScriptParser::DataType::CLASS &&
				p_type.class_type == p_base.class_type) {
			return true;
		}

		if (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type != nullptr) {
			resolve_class_inheritance(p_type.class_type);
			p_type = p_type.class_type->base_type;
			continue;
		}

		if (p_type.kind == GDScriptParser::DataType::SCRIPT &&
				p_base.kind == GDScriptParser::DataType::SCRIPT &&
				((!p_type.script_path.is_empty() && p_type.script_path == p_base.script_path) ||
						(p_type.script_type.is_valid() && p_type.script_type == p_base.script_type))) {
			return true;
		}

		if (p_type.kind == GDScriptParser::DataType::SCRIPT) {
			if (!p_type.script_path.is_empty()) {
				Ref<GDScriptParserRef> parser_ref = parser->get_depended_parser_for(p_type.script_path);
				if (parser_ref.is_valid() && parser_ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED) == OK) {
					p_type = parser_ref->get_parser()->head->base_type;
					continue;
				}
			}

			if (p_type.script_type.is_valid()) {
				Ref<Script> base_script = p_type.script_type->get_base_script();
				if (base_script.is_valid()) {
					GDScriptParser::DataType base_script_type;
					base_script_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
					base_script_type.kind = GDScriptParser::DataType::SCRIPT;
					base_script_type.builtin_type = Variant::OBJECT;
					base_script_type.native_type = base_script->get_instance_base_type();
					base_script_type.script_type = base_script;
					base_script_type.script_path = base_script->get_path();
					p_type = base_script_type;
					continue;
				}

				GDScriptParser::DataType native_base_type;
				native_base_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
				native_base_type.kind = GDScriptParser::DataType::NATIVE;
				native_base_type.builtin_type = Variant::OBJECT;
				native_base_type.native_type = p_type.script_type->get_instance_base_type();
				p_type = native_base_type;
				continue;
			}
		}

		break;
	}

	return false;
}

bool GDScriptAnalyzer::type_argument_satisfies_bound(const GDScriptParser::DataType &p_argument, const GDScriptParser::DataType &p_bound) {
	// A `Variant` bound imposes no requirement; any argument satisfies it.
	if (p_bound.is_variant()) {
		return true;
	}
	// A bound that is itself an (unsubstituted) type parameter — e.g. an outer-scope parameter the
	// caller could not bind — constrains the argument only by its own upper bound; an unbounded one
	// imposes nothing. Never fall through to the permissive general compatibility check below.
	if (p_bound.kind == GDScriptParser::DataType::TYPE_PARAMETER) {
		if (p_bound.type_parameter_bound.is_empty()) {
			return true;
		}
		// A nullable marker on the parameter handle itself (`U?`) widens its effective bound, so
		// carry it onto the unwrapped bound before recursing.
		GDScriptParser::DataType bound = p_bound.type_parameter_bound[0];
		bound.is_nullable = bound.is_nullable || p_bound.is_nullable;
		return type_argument_satisfies_bound(p_argument, bound);
	}
	if (p_argument.kind == GDScriptParser::DataType::TYPE_PARAMETER) {
		// A bare type parameter is erased to `Variant` at runtime, so it satisfies a concrete bound
		// only when its own declared upper bound provably does. The same strict rules apply to that
		// bound, so recurse rather than fall back to the permissive general compatibility check.
		if (p_argument.type_parameter_bound.is_empty()) {
			return false;
		}
		// A nullable type-parameter argument (`U?`) stays nullable through the unwrapping, so the
		// strict-null guard below still sees it.
		GDScriptParser::DataType argument = p_argument.type_parameter_bound[0];
		argument.is_nullable = argument.is_nullable || p_argument.is_nullable;
		return type_argument_satisfies_bound(argument, p_bound);
	}
	// A concrete `Variant` argument never satisfies a non-`Variant` bound.
	if (p_argument.is_variant()) {
		return false;
	}
	// Under strict null checks a nullable argument cannot satisfy a non-nullable bound: the bound
	// promises a non-null value while the argument admits null. This runs after the type-parameter
	// cases above so it compares fully-unwrapped concrete handles (a type-parameter bound that is
	// itself nullable, e.g. `T: U` with `U: RefCounted?`, has already been recursed into), and before
	// the trait/generic/derivation walks, which ignore `is_nullable`.
	if (strict_null_checks && p_argument.is_nullable && !p_bound.is_nullable) {
		return false;
	}
	// A trait bound is satisfied nominally: the argument must `use` the trait (directly, transitively,
	// or through an ancestor), never reach it by inheritance. Route to the dedicated trait check rather
	// than the derivation walk below, which would always reject a conforming `uses` class.
	if (p_bound.kind == GDScriptParser::DataType::CLASS && p_bound.class_type != nullptr && p_bound.class_type->is_trait) {
		return type_satisfies_trait(p_argument, p_bound);
	}
	// A generic bound (e.g. `List[int]`) must be matched invariantly in its type arguments, which the
	// general compatibility walk enforces along the inheritance chain; nominal derivation alone would
	// wrongly accept a `Stack[String]`. Plain (unspecialized) bounds keep the cheaper derivation check.
	if (p_bound.has_type_arguments()) {
		return is_type_compatible(p_bound, p_argument, false);
	}
	return datatype_derives_from_datatype(p_argument, p_bound);
}

bool GDScriptAnalyzer::class_satisfies_trait_base(GDScriptParser::ClassNode *p_class, GDScriptParser::ClassNode *p_trait) {
	if (p_class == nullptr || p_trait == nullptr) {
		return false;
	}
	if (!p_trait->extends_used) {
		return true;
	}
	return datatype_derives_from_datatype(p_class->base_type, p_trait->base_type);
}

bool GDScriptAnalyzer::type_satisfies_trait(const GDScriptParser::DataType &p_argument, const GDScriptParser::DataType &p_trait_bound) {
	// Trait conformance is nominal: the argument (or any ancestor) must declare the trait via `uses`,
	// so every class along the inheritance chain needs its `resolved_traits` populated before the shared
	// conformance walk inspects it. This check can run mid-inheritance-resolution (e.g. a subclass that
	// `extends Box[Sword]`), before the trait-use pass has reached each ancestor, so resolve each one
	// here. Resolving is idempotent; a class already mid-resolution is skipped to avoid a spurious cyclic
	// error and gets populated by the in-flight pass anyway.
	HashSet<GDScriptParser::ClassNode *> visited;
	for (GDScriptParser::ClassNode *ancestor = p_argument.class_type; ancestor != nullptr && !visited.has(ancestor);) {
		visited.insert(ancestor);
		if (!ancestor->resolving_trait_uses) {
			resolve_trait_uses(ancestor);
		}
		ancestor = ancestor->base_type.kind == GDScriptParser::DataType::CLASS ? ancestor->base_type.class_type : nullptr;
	}
	// Reuse the shared conformance machinery (`_class_has_trait`, reached through `is_type_compatible`),
	// which walks the inheritance chain and also accounts for externally scripted trait uses.
	return is_type_compatible(p_trait_bound, p_argument, false);
}

static bool _datatype_alpha_equal(const GDScriptParser::DataType &p_a, const GDScriptParser::DataType &p_b);

Error GDScriptAnalyzer::resolve_trait_uses(GDScriptParser::ClassNode *p_class, const GDScriptParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<GDScriptParserRef> parser_ref = ensure_cached_external_parser_for_class(p_class, nullptr,
			"Trying to resolve trait uses", p_source);

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			return ERR_PARSE_ERROR;
		}

		Error err = parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED);
		if (err != OK) {
			push_error(vformat(R"(Could not resolve trait uses for class "%s".)", p_class->fqcn), p_source);
			return err;
		}
		return OK;
	}

	if (p_class->resolved_trait_uses) {
		return OK;
	}
	if (p_class->failed_trait_uses) {
		return ERR_PARSE_ERROR;
	}
	auto fail = [&]() {
		p_class->resolving_trait_uses = false;
		p_class->failed_trait_uses = true;
		return ERR_PARSE_ERROR;
	};
	if (p_class->resolving_trait_uses) {
		push_error(vformat(R"(Could not resolve trait uses for "%s": Cyclic trait use.)",
						   _class_or_trait_name(p_class)),
				p_source);
		return fail();
	}

	if (resolve_class_inheritance(p_class, p_source) != OK) {
		return fail();
	}

	p_class->resolving_trait_uses = true;
	p_class->resolved_traits.clear();

	// A generic trait reached through more than one path must be bound to the same type arguments on
	// every path; otherwise the class would carry contradictory requirements (e.g. `Storage[int]` via
	// one supertrait and `Storage[String]` via another). Record each generic trait's binding the first
	// time it is seen and reject a later path that disagrees.
	HashMap<const GDScriptParser::ClassNode *, HashMap<StringName, GDScriptParser::DataType>> seen_trait_bindings;
	auto record_trait_binding = [&](const GDScriptParser::ClassNode *p_seen_trait, const HashMap<StringName, GDScriptParser::DataType> &p_binding, const GDScriptParser::Node *p_binding_source) -> bool {
		if (p_binding.is_empty()) {
			return true;
		}
		const HashMap<StringName, GDScriptParser::DataType> *previous = seen_trait_bindings.getptr(p_seen_trait);
		if (previous == nullptr) {
			seen_trait_bindings.insert(p_seen_trait, p_binding);
			return true;
		}
		for (const KeyValue<StringName, GDScriptParser::DataType> &entry : p_binding) {
			const GDScriptParser::DataType *previous_argument = previous->getptr(entry.key);
			if (previous_argument != nullptr && !_datatype_alpha_equal(entry.value, *previous_argument)) {
				push_error(vformat(R"(Trait "%s" is applied with conflicting type arguments ("%s" and "%s") through different traits used by "%s".)",
								   _class_or_trait_name(p_seen_trait), previous_argument->to_string(), entry.value.to_string(), _class_or_trait_name(p_class)),
						p_binding_source);
				return false;
			}
		}
		return true;
	};

	for (GDScriptParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		const GDScriptParser::Node *source = _trait_use_source(trait_use, p_class);
		GDScriptParser::ClassNode *trait = resolve_trait_reference(p_class, trait_use, source);
		if (trait == nullptr) {
			return fail();
		}
		if (resolve_trait_uses(trait, source) != OK) {
			return fail();
		}
		if (resolve_class_inheritance(trait, source) != OK) {
			return fail();
		}
		if (!class_satisfies_trait_base(p_class, trait)) {
			push_error(vformat(R"(Class "%s" cannot use trait "%s" because it does not inherit from "%s".)",
							   _class_or_trait_name(p_class), _class_or_trait_name(trait),
							   trait->base_type.to_string()),
					source);
			return fail();
		}

		// Specialize a generic trait at the use site: `uses Container[int]`. The arguments are resolved
		// in this class's scope, validated against the trait's parameter arity and bounds, and stored so
		// trait-requirement conformance can substitute them into the trait's `T`-typed members.
		if (!trait_use.type_arguments.is_empty()) {
			if (trait->type_parameters.is_empty()) {
				push_error(vformat(R"(Trait "%s" is not generic and cannot take type arguments.)", _class_or_trait_name(trait)), trait_use.type_arguments[0]);
				return fail();
			}
			GDScriptParser::DataType trait_handle = type_from_metatype(trait->get_datatype());
			trait_handle.is_meta_type = false;
			// Resolve the type arguments in `p_class`'s scope so a generic trait can forward its own
			// type parameter into a generic supertrait (`trait Wrapper[T] uses Storage[T]`).
			GDScriptParser::ClassNode *previous_class = parser->current_class;
			parser->current_class = p_class;
			const bool applied = apply_class_type_arguments(trait_handle, trait_use.type_arguments, trait_use.type_arguments[0]);
			parser->current_class = previous_class;
			if (!applied) {
				return fail();
			}
			trait_use.resolved_type_arguments = trait_handle.type_arguments;
		}

		_append_trait_unique(p_class->resolved_traits, trait);

		// The use-site binding of the directly-applied trait's own parameters, used to re-specialize
		// the bindings its supertraits carry into this class's frame.
		HashMap<StringName, GDScriptParser::DataType> direct_substitution;
		if (!trait->type_parameters.is_empty() && !trait_use.resolved_type_arguments.is_empty()) {
			const int count = MIN(trait->type_parameters.size(), trait_use.resolved_type_arguments.size());
			for (int i = 0; i < count; i++) {
				const GDScriptParser::TypeParameterNode *type_parameter = trait->type_parameters[i];
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					direct_substitution.insert(type_parameter->identifier->name, trait_use.resolved_type_arguments[i]);
				}
			}
		}
		if (!record_trait_binding(trait, direct_substitution, source)) {
			return fail();
		}

		for (GDScriptParser::ClassNode *transitive_trait : trait->resolved_traits) {
			if (!class_satisfies_trait_base(p_class, transitive_trait)) {
				push_error(vformat(R"(Class "%s" cannot use trait "%s" because it does not inherit from "%s".)",
								   _class_or_trait_name(p_class), _class_or_trait_name(transitive_trait),
								   transitive_trait->base_type.to_string()),
						source);
				return fail();
			}
			// Re-specialize how `trait` binds this supertrait into the class's frame, then check it
			// against any binding the supertrait already received through another path.
			HashMap<StringName, GDScriptParser::DataType> transitive_binding;
			for (const KeyValue<StringName, GDScriptParser::DataType> &entry : trait_type_argument_substitution(trait, transitive_trait)) {
				transitive_binding.insert(entry.key, GDScriptParser::DataType::substitute(entry.value, direct_substitution));
			}
			if (!record_trait_binding(transitive_trait, transitive_binding, source)) {
				return fail();
			}
			_append_trait_unique(p_class->resolved_traits, transitive_trait);
		}
	}

	p_class->resolving_trait_uses = false;
	p_class->resolved_trait_uses = true;
	p_class->failed_trait_uses = false;
	return OK;
}

Error GDScriptAnalyzer::resolve_trait_uses(GDScriptParser::ClassNode *p_class, bool p_recursive) {
	Error err = resolve_trait_uses(p_class);
	if (err != OK) {
		return err;
	}

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			if (p_class->members[i].type == GDScriptParser::ClassNode::Member::CLASS) {
				err = resolve_trait_uses(p_class->members[i].m_class, true);
				if (err != OK) {
					return err;
				}
			}
		}
	}

	return OK;
}

bool GDScriptAnalyzer::find_trait_implementation(GDScriptParser::ClassNode *p_class, const StringName &p_function_name,
		TraitMethodImplementation &r_implementation) {
	r_implementation = TraitMethodImplementation();
	GDScriptParser::ClassNode *current_class = p_class;
	HashSet<GDScriptParser::ClassNode *> visited_classes;
	while (current_class != nullptr) {
		if (visited_classes.has(current_class)) {
			break;
		}
		visited_classes.insert(current_class);

		if (current_class->has_function(p_function_name)) {
			GDScriptParser::FunctionNode *function = current_class->get_member(p_function_name).function;
			if (!function->is_abstract) {
				if (parser->has_class(current_class)) {
					resolve_function_signature_in_class(function, current_class, function);
					r_implementation.function = function;
					r_implementation.owner_class = current_class;
				} else {
					r_implementation.method_info = function->info;
					r_implementation.method_info_source = _trait_method_info_source(current_class, function);
					r_implementation.has_method_info = true;
				}
				return true;
			}
		}

		resolve_class_inheritance(current_class);
		if (current_class->base_type.kind == GDScriptParser::DataType::CLASS) {
			current_class = current_class->base_type.class_type;
		} else if (current_class->base_type.kind == GDScriptParser::DataType::SCRIPT) {
			if (!current_class->base_type.script_path.is_empty()) {
				Ref<GDScriptParserRef> base_parser_ref = parser->get_depended_parser_for(current_class->base_type.script_path);
				if (base_parser_ref.is_valid() && base_parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED) == OK) {
					current_class = base_parser_ref->get_parser()->head;
					continue;
				}
			}

			Ref<Script> script = current_class->base_type.script_type;
			while (script.is_valid()) {
				MethodInfo info = script->get_method_info(p_function_name);
				if (info.name == p_function_name) {
					r_implementation.method_info = info;
					if (!script->get_path().is_empty()) {
						r_implementation.method_info_source = vformat(R"(Implementation comes from "%s".)",
								_localize_script_path(script->get_path()));
					}
					r_implementation.has_method_info = true;
					return true;
				}
				script = script->get_base_script();
			}
			current_class = nullptr;
		} else {
			MethodInfo info;
			if (ClassDB::get_method_info(current_class->base_type.native_type, p_function_name, &info)) {
				r_implementation.method_info = info;
				r_implementation.method_info_source = vformat(R"(Implementation comes from native class "%s".)", current_class->base_type.native_type);
				r_implementation.has_method_info = true;
				return true;
			}
			current_class = nullptr;
		}
	}

	return false;
}

static bool _signature_type_involves_type_parameter(const GDScriptParser::DataType &p_type) {
	if (p_type.kind == GDScriptParser::DataType::TYPE_PARAMETER) {
		return true;
	}
	for (const GDScriptParser::DataType &element : p_type.container_element_types) {
		if (_signature_type_involves_type_parameter(element)) {
			return true;
		}
	}
	for (const GDScriptParser::DataType &argument : p_type.type_arguments) {
		if (_signature_type_involves_type_parameter(argument)) {
			return true;
		}
	}
	// A Callable/Signal signature can hide a type parameter in its parameter or return types
	// (e.g. `Callable[[U], void]`).
	for (const GDScriptParser::DataType &parameter_type : p_type.method_parameter_types) {
		if (_signature_type_involves_type_parameter(parameter_type)) {
			return true;
		}
	}
	for (const GDScriptParser::DataType &return_type : p_type.method_return_type) {
		if (_signature_type_involves_type_parameter(return_type)) {
			return true;
		}
	}
	return false;
}

// Deep structural (alpha-)equality of two resolved types. DataType::operator== ignores Callable/Signal
// method signatures (and compares nested container/type-argument elements via operator== too, so the
// blind spot is recursive), which would let `Callable[[U], void]` match `Callable[[int], int]`. This
// recurses through every sub-type so a trait-required generic signature is matched exactly.
static bool _datatype_alpha_equal(const GDScriptParser::DataType &p_a, const GDScriptParser::DataType &p_b) {
	if (!(p_a == p_b)) {
		return false;
	}
	if (p_a.has_method_signature != p_b.has_method_signature) {
		return false;
	}
	if (p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size()) {
		return false;
	}
	for (int i = 0; i < p_a.type_parameter_bound.size(); i++) {
		if (!_datatype_alpha_equal(p_a.type_parameter_bound[i], p_b.type_parameter_bound[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.container_element_types.size(); i++) {
		if (!_datatype_alpha_equal(p_a.container_element_types[i], p_b.container_element_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.type_arguments.size(); i++) {
		if (!_datatype_alpha_equal(p_a.type_arguments[i], p_b.type_arguments[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_parameter_types.size(); i++) {
		if (!_datatype_alpha_equal(p_a.method_parameter_types[i], p_b.method_parameter_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_return_type.size(); i++) {
		if (!_datatype_alpha_equal(p_a.method_return_type[i], p_b.method_return_type[i])) {
			return false;
		}
	}
	return true;
}

HashMap<StringName, GDScriptParser::DataType> GDScriptAnalyzer::trait_type_argument_substitution(GDScriptParser::ClassNode *p_class, GDScriptParser::ClassNode *p_trait) {
	HashMap<StringName, GDScriptParser::DataType> bindings;
	if (p_class == nullptr || p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}
	// Resolve `p_trait`'s use-site type arguments as seen from `p_class`. A directly-applied generic
	// trait (`uses Container[int]`) binds its parameters here; a transitive generic supertrait
	// (`uses Wrapper` where `Wrapper uses Storage[int]`) is bound by the intermediate trait that
	// applies it, so we recurse through the intermediate and compose the two substitutions.
	for (const GDScriptParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		GDScriptParser::ClassNode *used_trait = trait_use.resolved_trait;
		if (used_trait == nullptr) {
			continue;
		}
		if (used_trait == p_trait) {
			if (trait_use.resolved_type_arguments.is_empty()) {
				continue;
			}
			const int count = MIN(p_trait->type_parameters.size(), trait_use.resolved_type_arguments.size());
			for (int i = 0; i < count; i++) {
				const GDScriptParser::TypeParameterNode *type_parameter = p_trait->type_parameters[i];
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					bindings.insert(type_parameter->identifier->name, trait_use.resolved_type_arguments[i]);
				}
			}
			return bindings;
		}
		if (used_trait->resolved_traits.has(p_trait)) {
			// `used_trait` (transitively) applies `p_trait`. First find how `used_trait` binds
			// `p_trait`, then re-specialize those arguments with `p_class`'s binding of
			// `used_trait`'s own parameters (`uses Wrapper[int]` forwarding `T` into `Storage[T]`).
			HashMap<StringName, GDScriptParser::DataType> inner = trait_type_argument_substitution(used_trait, p_trait);
			if (inner.is_empty()) {
				continue;
			}
			const HashMap<StringName, GDScriptParser::DataType> outer = trait_type_argument_substitution(p_class, used_trait);
			for (const KeyValue<StringName, GDScriptParser::DataType> &binding : inner) {
				bindings.insert(binding.key, GDScriptParser::DataType::substitute(binding.value, outer));
			}
			return bindings;
		}
	}
	return bindings;
}

bool GDScriptAnalyzer::validate_trait_method_signature(GDScriptParser::ClassNode *p_trait,
		GDScriptParser::FunctionNode *p_required_function,
		const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, GDScriptParser::DataType> &p_trait_substitution) {
	resolve_function_signature_in_class(p_required_function, p_trait, p_required_function);
	if (p_implementation.has_method_info) {
		return validate_trait_method_info_signature(p_trait, p_required_function, p_implementation, p_trait_substitution);
	}

	GDScriptParser::FunctionNode *implementation_function = p_implementation.function;
	resolve_function_signature_in_class(implementation_function, p_implementation.owner_class, implementation_function);

	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = _class_or_trait_name(p_trait) + "." + String(function_name) + "()";
	const bool required_is_coroutine = p_required_function->is_coroutine;
	const bool implementation_is_coroutine = implementation_function->is_coroutine;
	if (required_is_coroutine != implementation_is_coroutine) {
		if (required_is_coroutine) {
			push_error(vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		} else {
			push_error(vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		}
		return false;
	}

	bool valid = p_required_function->is_static == implementation_function->is_static;

	// The trait's use-site bindings (`T := int`) specialize the required signature. A generic required
	// method may declare a type parameter that shadows a trait parameter by name; that method-scoped
	// parameter keeps its own identity, so drop any shadowed names before substituting.
	HashMap<StringName, GDScriptParser::DataType> method_trait_substitution = p_trait_substitution;
	for (const GDScriptParser::TypeParameterNode *required_type_parameter : p_required_function->type_parameters) {
		if (required_type_parameter != nullptr && required_type_parameter->identifier != nullptr) {
			method_trait_substitution.erase(required_type_parameter->identifier->name);
		}
	}

	// A trait may require a generic method; an implementation satisfies it up to type-parameter
	// renaming (alpha-equivalence by ordinal position). The lists must share arity, the bound at each
	// position must align, and the implementation's type parameters are renamed onto the required ones
	// so `map[V](...) -> Array[V]` matches required `map[U](...) -> Array[U]`. The renaming is applied
	// to the implementation's parameter/return types before the per-type compatibility checks below.
	HashMap<StringName, GDScriptParser::DataType> type_parameter_renaming;
	{
		const Vector<GDScriptParser::TypeParameterNode *> &required_type_parameters = p_required_function->type_parameters;
		const Vector<GDScriptParser::TypeParameterNode *> &implementation_type_parameters = implementation_function->type_parameters;
		if (required_type_parameters.size() != implementation_type_parameters.size()) {
			valid = false;
		} else {
			// Method type parameters carry no eager resolved_bound, so read the bound TypeNode's
			// (meta-stripped) datatype.
			auto bound_of = [](const GDScriptParser::TypeParameterNode *p_type_parameter) -> GDScriptParser::DataType {
				GDScriptParser::DataType bound;
				if (p_type_parameter != nullptr && p_type_parameter->bound != nullptr) {
					bound = p_type_parameter->bound->get_datatype();
					bound.is_meta_type = false;
				}
				return bound;
			};
			// Pass 1: build the renaming of the implementation's type parameters onto the required ones
			// (same method scope and ordinal index), carrying the required bound so the structural
			// equality used below — which compares type_parameter_bound — aligns after substitution.
			for (int i = 0; i < required_type_parameters.size(); i++) {
				const GDScriptParser::TypeParameterNode *required_type_parameter = required_type_parameters[i];
				const GDScriptParser::TypeParameterNode *implementation_type_parameter = implementation_type_parameters[i];
				if (required_type_parameter == nullptr || required_type_parameter->identifier == nullptr ||
						implementation_type_parameter == nullptr || implementation_type_parameter->identifier == nullptr) {
					continue;
				}
				GDScriptParser::DataType required_handle;
				required_handle.kind = GDScriptParser::DataType::TYPE_PARAMETER;
				required_handle.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
				required_handle.type_parameter_name = required_type_parameter->identifier->name;
				required_handle.type_parameter_scope = GDScriptParser::DataType::TYPE_PARAMETER_METHOD;
				required_handle.type_parameter_index = i;
				const GDScriptParser::DataType required_bound = GDScriptParser::DataType::substitute(bound_of(required_type_parameter), method_trait_substitution);
				if (required_bound.is_set() && !required_bound.is_variant()) {
					required_handle.type_parameter_bound.push_back(required_bound);
				}
				type_parameter_renaming.insert(implementation_type_parameter->identifier->name, required_handle);
			}
			// Pass 2: each position's bound must align after renaming, so a dependent bound like
			// `[U: Resource, T: U]` matches `[V: Resource, W: V]`. A bound that involves a type
			// parameter is compared by alpha-equivalence (structural equality after renaming); a
			// concrete bound by ordinary mutual compatibility.
			for (int i = 0; i < required_type_parameters.size(); i++) {
				const GDScriptParser::DataType required_bound = GDScriptParser::DataType::substitute(bound_of(required_type_parameters[i]), method_trait_substitution);
				const GDScriptParser::DataType implementation_bound = GDScriptParser::DataType::substitute(bound_of(implementation_type_parameters[i]), type_parameter_renaming);
				const bool required_has_bound = required_bound.is_set() && !required_bound.is_variant();
				const bool implementation_has_bound = implementation_bound.is_set() && !implementation_bound.is_variant();
				if (required_has_bound != implementation_has_bound) {
					valid = false;
				} else if (required_has_bound) {
					// Bounds must be the same after renaming — compared by deep structural equality so a
					// difference hidden in a Callable/Signal bound signature is not lost.
					valid = valid && _datatype_alpha_equal(required_bound, implementation_bound);
				}
			}
		}
	}

	// A generic method's signature is matched by alpha-equivalence (exact structural match after
	// renaming), not the lenient subtype compatibility used for ordinary methods — otherwise a bare
	// type-parameter return (`-> U`) would be leniently accepted against `-> Array[U]`.
	const bool is_generic_method = !p_required_function->type_parameters.is_empty() || !implementation_function->type_parameters.is_empty();

	if (p_required_function->return_type != nullptr) {
		// Specialize the required signature with the generic trait's use-site arguments (`T := int`)
		// before comparing it to the implementation.
		const GDScriptParser::DataType required_return_type = GDScriptParser::DataType::substitute(p_required_function->get_datatype(), method_trait_substitution);
		const GDScriptParser::DataType implementation_return_type = GDScriptParser::DataType::substitute(implementation_function->get_datatype(), type_parameter_renaming);
		if (is_generic_method && (_signature_type_involves_type_parameter(required_return_type) || _signature_type_involves_type_parameter(implementation_return_type))) {
			// A type-parameter-involving return must match by alpha-equivalence (so `-> V` does not
			// leniently satisfy `-> Array[U]`); concrete returns keep their covariant matching below.
			valid = valid && _datatype_alpha_equal(required_return_type, implementation_return_type);
		} else if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (implementation_return_type.kind == GDScriptParser::DataType::BUILTIN &&
				implementation_return_type.builtin_type == Variant::NIL) {
			if (required_return_type.is_hard_type() &&
					!(required_return_type.kind == GDScriptParser::DataType::BUILTIN &&
							required_return_type.builtin_type == Variant::NIL)) {
				valid = false;
			}
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && is_type_compatible(required_return_type, implementation_return_type);
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = implementation_function->parameters.size() - implementation_function->default_arg_values.size();
	const int implementation_max_argc = implementation_function->is_vararg() ? INT_MAX : implementation_function->parameters.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < implementation_function->parameters.size(); i++) {
			const GDScriptParser::DataType required_parameter_type = GDScriptParser::DataType::substitute(p_required_function->parameters[i]->datatype, method_trait_substitution);
			const GDScriptParser::DataType implementation_parameter_type = GDScriptParser::DataType::substitute(implementation_function->parameters[i]->datatype, type_parameter_renaming);
			if (is_generic_method && (_signature_type_involves_type_parameter(required_parameter_type) || _signature_type_involves_type_parameter(implementation_parameter_type))) {
				// A type-parameter-involving parameter must match by alpha-equivalence; concrete
				// parameters keep their contravariant matching below.
				valid = valid && _datatype_alpha_equal(required_parameter_type, implementation_parameter_type);
			} else if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && is_type_compatible(implementation_parameter_type, required_parameter_type);
			}
		}
	}

	if (!valid) {
		push_error(vformat(R"*(The function "%s()" signature does not match required trait method "%s".)*",
						   function_name, trait_method_name),
				implementation_function);
		return false;
	}

	return true;
}

bool GDScriptAnalyzer::validate_trait_method_info_signature(GDScriptParser::ClassNode *p_trait,
		GDScriptParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, GDScriptParser::DataType> &p_trait_substitution) {
	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = _class_or_trait_name(p_trait) + "." + String(function_name) + "()";

	// A MethodInfo carries no generic type-parameter information, so a generic trait requirement
	// cannot be verified up to renaming through this path and is therefore not satisfiable by it.
	if (!p_required_function->type_parameters.is_empty()) {
		String message = vformat(R"*(The function "%s()" signature does not match required generic trait method "%s".)*", function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	const bool required_is_coroutine = p_required_function->is_coroutine;
	const bool implementation_is_coroutine = (p_implementation.method_info.flags & METHOD_FLAG_ASYNC) != 0;
	if (required_is_coroutine != implementation_is_coroutine) {
		String message;
		if (required_is_coroutine) {
			message = vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
					function_name, trait_method_name);
		} else {
			message = vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
					function_name, trait_method_name);
		}
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	bool valid = (p_required_function->is_static == ((p_implementation.method_info.flags & METHOD_FLAG_STATIC) != 0));

	if (p_required_function->return_type != nullptr) {
		const GDScriptParser::DataType required_return_type = GDScriptParser::DataType::substitute(p_required_function->get_datatype(), p_trait_substitution);
		GDScriptParser::DataType implementation_return_type = type_from_property(p_implementation.method_info.return_val);
		if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && is_type_compatible(required_return_type, implementation_return_type);
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = p_implementation.method_info.arguments.size() - p_implementation.method_info.default_arguments.size();
	const int implementation_max_argc = (p_implementation.method_info.flags & METHOD_FLAG_VARARG) ? INT_MAX : p_implementation.method_info.arguments.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < p_implementation.method_info.arguments.size(); i++) {
			const GDScriptParser::DataType required_parameter_type = GDScriptParser::DataType::substitute(p_required_function->parameters[i]->datatype, p_trait_substitution);
			const GDScriptParser::DataType implementation_parameter_type = type_from_property(p_implementation.method_info.arguments[i], true);
			if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && is_type_compatible(implementation_parameter_type, required_parameter_type);
			}
		}
	}

	if (!valid) {
		String message = vformat(R"*(The native function "%s()" signature does not match required trait method "%s".)*",
				function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	return true;
}

static bool _trait_member_is_state(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::VARIABLE:
		case GDScriptParser::ClassNode::Member::CONSTANT:
		case GDScriptParser::ClassNode::Member::ENUM:
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return true;
		default:
			return false;
	}
}

struct TraitMemberSource {
	GDScriptParser::ClassNode *trait = nullptr;
	GDScriptParser::ClassNode::Member member;
};

// Compares the declared types of a class member and a trait member it redeclares, ignoring
// `type_source`. DataType::operator== treats INFERRED/UNDETECTED operands as equal for parsing
// purposes, which would let an inferred-but-incompatible redeclaration (e.g. `var health = "x"`
// against a trait's `var health: int`) slip through, so the structural identity is compared here.
static bool _trait_state_type_is_compatible(const GDScriptParser::DataType &p_trait_type, const GDScriptParser::DataType &p_class_type) {
	// A genuinely untyped redeclaration can hold the trait's value, so it is not a conflict.
	if (p_trait_type.kind == GDScriptParser::DataType::VARIANT || p_class_type.kind == GDScriptParser::DataType::VARIANT) {
		return true;
	}
	if (p_trait_type.kind != p_class_type.kind) {
		return false;
	}
	switch (p_class_type.kind) {
		case GDScriptParser::DataType::BUILTIN:
			return p_trait_type.builtin_type == p_class_type.builtin_type &&
					p_trait_type.container_element_types == p_class_type.container_element_types;
		case GDScriptParser::DataType::NATIVE:
		case GDScriptParser::DataType::ENUM:
			return p_trait_type.native_type == p_class_type.native_type;
		case GDScriptParser::DataType::SCRIPT:
			return p_trait_type.script_type == p_class_type.script_type;
		case GDScriptParser::DataType::CLASS:
			return p_trait_type.class_type == p_class_type.class_type ||
					(p_trait_type.class_type != nullptr && p_class_type.class_type != nullptr &&
							p_trait_type.class_type->fqcn == p_class_type.class_type->fqcn);
		default:
			return true;
	}
}

void GDScriptAnalyzer::validate_trait_conflicts(GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_traits.is_empty()) {
		return;
	}

	// Traits and abstract classes are allowed to defer implementation and disambiguation to a
	// concrete subclass, mirroring validate_trait_requirements, so they must not raise conflicts.
	if (p_class->is_trait || p_class->is_abstract) {
		return;
	}

	HashMap<StringName, TraitMemberSource> trait_methods;
	HashMap<StringName, TraitMemberSource> trait_state;

	for (GDScriptParser::ClassNode *trait : p_class->resolved_traits) {
		resolve_class_interface(trait, p_class);

		for (const GDScriptParser::ClassNode::Member &member : trait->members) {
			if (member.type != GDScriptParser::ClassNode::Member::FUNCTION && !_trait_member_is_state(member)) {
				continue;
			}

			const StringName member_name = StringName(member.get_name());
			if (member_name == StringName()) {
				continue;
			}

			// A trait member that the class does not itself redeclare will be flattened
			// in, so it must not collide with a member of the implementer's base classes
			// or native base — the same diagnostic the class's own members would raise.
			// (A method overriding a base method is allowed, as for normal classes.)
			if (!p_class->has_member(member_name) &&
					check_class_member_name_conflict(p_class, member_name, member.get_source_node()) != OK) {
				continue;
			}

			if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
				if (member.function == nullptr) {
					continue;
				}

				if (p_class->has_member(member_name)) {
					const GDScriptParser::ClassNode::Member class_member = p_class->get_member(member_name);
					if (class_member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
						push_error(vformat(R"*(Class "%s" redeclares trait method "%s()" from "%s" with a %s member.)*",
										   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait),
										   class_member.get_type_name()),
								class_member.get_source_node());
						continue;
					}

					if (!member.function->is_abstract) {
						TraitMethodImplementation implementation;
						implementation.function = class_member.function;
						implementation.owner_class = p_class;
						validate_trait_method_signature(trait, member.function, implementation, trait_type_argument_substitution(p_class, trait));
					}
					continue;
				}

				if (member.function->is_abstract) {
					continue;
				}

				bool inherited_method_shadows_trait = false;
				for (GDScriptParser::DataType *base_type = &p_class->base_type;
						base_type != nullptr && base_type->kind == GDScriptParser::DataType::CLASS;) {
					GDScriptParser::ClassNode *base_class = base_type->class_type;
					if (base_class == nullptr) {
						break;
					}

					if (base_class->has_function(member_name)) {
						GDScriptParser::ClassNode::Member base_member = base_class->get_member(member_name);
						if (base_member.function != nullptr && !base_member.function->is_abstract) {
							TraitMethodImplementation implementation;
							implementation.function = base_member.function;
							implementation.owner_class = base_class;
							validate_trait_method_signature(trait, member.function, implementation, trait_type_argument_substitution(p_class, trait));
							inherited_method_shadows_trait = true;
							break;
						}
					}

					resolve_class_inheritance(base_class);
					base_type = &base_class->base_type;
				}
				if (inherited_method_shadows_trait) {
					continue;
				}

				HashMap<StringName, TraitMemberSource>::Iterator previous = trait_methods.find(member_name);
				if (previous) {
					push_error(vformat(R"*(Trait method "%s()" from "%s" conflicts with trait method "%s()" from "%s"; override it in "%s" to disambiguate.)*",
									   member_name, _class_or_trait_name(previous->value.trait), member_name,
									   _class_or_trait_name(trait), _class_or_trait_name(p_class)),
							_trait_requirement_source(p_class, trait));
					continue;
				}

				TraitMemberSource source;
				source.trait = trait;
				source.member = member;
				trait_methods.insert(member_name, source);
				continue;
			}

			if (p_class->has_member(member_name)) {
				const GDScriptParser::ClassNode::Member class_member = p_class->get_member(member_name);
				if (class_member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
					push_error(vformat(R"(Class "%s" redeclares trait member "%s" from "%s" with a function.)",
									   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait)),
							class_member.get_source_node());
					continue;
				}

				const GDScriptParser::DataType trait_type = member.get_datatype();
				const GDScriptParser::DataType class_type = class_member.get_datatype();
				if (!_trait_state_type_is_compatible(trait_type, class_type)) {
					push_error(vformat(R"(Class "%s" redeclares trait member "%s" from "%s" with incompatible type. Expected "%s", got "%s".)",
									   _class_or_trait_name(p_class), member_name, _class_or_trait_name(trait),
									   trait_type.to_string(), class_type.to_string()),
							class_member.get_source_node());
				}
				continue;
			}

			HashMap<StringName, TraitMemberSource>::Iterator previous = trait_state.find(member_name);
			if (previous) {
				push_error(vformat(R"(Trait member "%s" from "%s" conflicts with trait member "%s" from "%s"; redeclare it in "%s" with type "%s" to disambiguate.)",
								   member_name, _class_or_trait_name(previous->value.trait), member_name,
								   _class_or_trait_name(trait), _class_or_trait_name(p_class),
								   previous->value.member.get_datatype().to_string()),
						_trait_requirement_source(p_class, trait));
				continue;
			}

			TraitMemberSource source;
			source.trait = trait;
			source.member = member;
			trait_state.insert(member_name, source);
		}
	}
}

void GDScriptAnalyzer::validate_trait_requirements(GDScriptParser::ClassNode *p_class) {
	if (p_class->is_trait || p_class->is_abstract) {
		return;
	}

	// A trait's base class is an inheritance constraint. Abstract methods inherited
	// from that base are enforced by the normal base-class abstract checks once the
	// using class derives from it.
	// This pass validates trait requirements only. Concrete trait methods are not merged
	// into the using class, so a concrete method on one used trait does not implement
	// an abstract method required by another used trait.
	HashSet<StringName> missing_trait_methods;
	for (GDScriptParser::ClassNode *trait : p_class->resolved_traits) {
		for (GDScriptParser::ClassNode::Member member : trait->members) {
			if (member.type != GDScriptParser::ClassNode::Member::FUNCTION ||
					member.function == nullptr || !member.function->is_abstract) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (!find_trait_implementation(p_class, member.function->identifier->name, implementation)) {
				const StringName function_name = member.function->identifier->name;
				if (!missing_trait_methods.has(function_name)) {
					missing_trait_methods.insert(function_name);
					push_error(vformat(R"*(Class "%s" must implement trait method "%s.%s()".)*",
									   _class_or_trait_name(p_class), _class_or_trait_name(trait),
									   function_name),
							_trait_requirement_source(p_class, trait));
				}
				continue;
			}

			validate_trait_method_signature(trait, member.function, implementation, trait_type_argument_substitution(p_class, trait));
		}
	}
}

Error GDScriptAnalyzer::validate_imports() {
	if (parser->head->imports.is_empty()) {
		return OK;
	}

	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	LocalVector<String> checked_imports;

	for (const String &import : parser->head->imports) {
		if (_string_vector_has(checked_imports, import)) {
			continue;
		}
		checked_imports.push_back(import);

		// A namespace is a valid import target when it exposes any global class/trait or any
		// custom annotation declaration. Annotation-only libraries declare no `class_name`, so
		// they would otherwise be invisible to import validation.
		if (!_namespace_exists_in_global_classes(global_classes, import) &&
				!GDScriptLanguage::get_singleton()->namespace_has_annotations(import)) {
			push_error(vformat(R"(Could not find imported namespace "%s".)", import), parser->head);
		}
	}

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

Error GDScriptAnalyzer::validate_annotation_declarations() {
	if (parser->head->annotation_declarations.is_empty()) {
		return OK;
	}

	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	HashSet<String> declared_in_file;

	for (GDScriptParser::AnnotationDeclarationNode *declaration : parser->head->annotation_declarations) {
		if (declaration->identifier == nullptr) {
			continue;
		}

		const StringName short_name = declaration->identifier->name;

		// Built-in annotation names (registered with the leading `@`) stay reserved in the
		// annotation-symbol space so custom declarations can never shadow engine behavior.
		if (parser->valid_annotations.has(StringName("@" + String(short_name)))) {
			push_error(vformat(R"(Cannot declare custom annotation "%s": "@%s" is a built-in annotation.)", short_name, short_name), declaration);
			continue;
		}

		const String &qualified_name = declaration->qualified_name;
		if (qualified_name.is_empty()) {
			continue;
		}

		if (declared_in_file.has(qualified_name)) {
			push_error(vformat(R"(Duplicate annotation declaration "%s".)", qualified_name), declaration);
			continue;
		}
		declared_in_file.insert(qualified_name);

		// A canonical identity registered by two or more distinct files is a hard error: imports
		// could not disambiguate between the declarations.
		if (language != nullptr && language->is_duplicated_global_annotation(StringName(qualified_name))) {
			push_error(vformat(R"(Duplicate annotation declaration "%s": the same canonical annotation is declared in another file.)", qualified_name), declaration);
		}
	}

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

#ifdef DEBUG_ENABLED
void GDScriptAnalyzer::validate_mixed_namespace_directory() {
	if (parser->head->get_global_name() == StringName() || parser->script_path.is_empty()) {
		return;
	}
	if (parser->is_project_ignoring_warnings ||
			parser->warning_levels[GDScriptWarning::MIXED_NAMESPACE_DIRECTORY] == GDScriptWarning::IGNORE) {
		return;
	}

	const String current_path = GDScript::canonicalize_path(parser->script_path);
	const String current_dir = current_path.get_base_dir();
	if (current_dir.is_empty()) {
		return;
	}

	Vector<String> namespaces;
	namespaces.push_back(_get_namespace_warning_name(parser->head->namespace_name));

	_update_mixed_namespace_directory_cache();

	const Vector<MixedNamespaceDirectoryClass> *directory_classes = mixed_namespace_directory_cache.classes_by_directory.getptr(current_dir);
	if (directory_classes != nullptr) {
		for (const MixedNamespaceDirectoryClass &class_info : *directory_classes) {
			if (class_info.script_path == current_path) {
				continue;
			}

			if (!namespaces.has(class_info.namespace_name)) {
				namespaces.push_back(class_info.namespace_name);
			}
		}
	}

	if (namespaces.size() < 2) {
		return;
	}

	// GDScript warnings are source-file diagnostics, so this intentionally emits once per analyzed global script class.
	parser->push_warning(parser->head, GDScriptWarning::MIXED_NAMESPACE_DIRECTORY, current_dir, _format_namespace_warning_list(namespaces));
}
#endif // DEBUG_ENABLED

Ref<GDScriptParserRef> GDScriptAnalyzer::ensure_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_from_class, const char *p_context, const GDScriptParser::Node *p_source) {
	// Delicate piece of code that intentionally doesn't use the GDScript cache or `get_depended_parser_for`.
	// Search dependencies for the parser that owns `p_class` and make a cache entry for it.
	// Required for how we store pointers to classes owned by other parser trees and need to call `resolve_class_member` and such on the same parser tree.
	// Since https://github.com/godotengine/godot/pull/94871 there can technically be multiple parsers for the same script in the same parser tree.
	// Even if unlikely, getting the wrong parser could lead to strange undefined behavior without errors.

	if (p_class == nullptr) {
		return nullptr;
	}

	if (HashMap<const GDScriptParser::ClassNode *, Ref<GDScriptParserRef>>::Iterator E = external_class_parser_cache.find(p_class)) {
		return E->value;
	}

	if (parser->has_class(p_class)) {
		return nullptr;
	}

	if (p_from_class == nullptr) {
		p_from_class = parser->head;
	}

	Ref<GDScriptParserRef> parser_ref;
	for (const GDScriptParser::ClassNode *look_class = p_from_class; look_class != nullptr; look_class = look_class->base_type.class_type) {
		if (parser->has_class(look_class)) {
			parser_ref = find_cached_external_parser_for_class(p_class, parser);
			if (parser_ref.is_valid()) {
				break;
			}
		}

		if (HashMap<const GDScriptParser::ClassNode *, Ref<GDScriptParserRef>>::Iterator E = external_class_parser_cache.find(look_class)) {
			parser_ref = find_cached_external_parser_for_class(p_class, E->value);
			if (parser_ref.is_valid()) {
				break;
			}
		}

		String look_class_script_path = look_class->get_datatype().script_path;
		if (HashMap<String, Ref<GDScriptParserRef>>::Iterator E = parser->depended_parsers.find(look_class_script_path)) {
			parser_ref = find_cached_external_parser_for_class(p_class, E->value);
			if (parser_ref.is_valid()) {
				break;
			}
		}
	}

	if (parser_ref.is_null()) {
		push_error(vformat(R"(Parser bug (please report): Could not find external parser for class "%s". (%s))", p_class->fqcn, p_context), p_source);
		// A null parser will be inserted into the cache, so this error won't spam for the same class.
		// This is ok, the values of external_class_parser_cache are not assumed to be valid references.
	}

	external_class_parser_cache.insert(p_class, parser_ref);
	return parser_ref;
}

Ref<GDScriptParserRef> GDScriptAnalyzer::find_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, const Ref<GDScriptParserRef> &p_dependant_parser) {
	if (p_dependant_parser.is_null()) {
		return nullptr;
	}

	if (HashMap<const GDScriptParser::ClassNode *, Ref<GDScriptParserRef>>::Iterator E = p_dependant_parser->get_analyzer()->external_class_parser_cache.find(p_class)) {
		if (E->value.is_valid()) {
			// Silently ensure it's parsed.
			E->value->raise_status(GDScriptParserRef::PARSED);
			if (E->value->get_parser()->has_class(p_class)) {
				return E->value;
			}
		}
	}

	if (p_dependant_parser->get_parser()->has_class(p_class)) {
		return p_dependant_parser;
	}

	// Silently ensure it's parsed.
	p_dependant_parser->raise_status(GDScriptParserRef::PARSED);
	return find_cached_external_parser_for_class(p_class, p_dependant_parser->get_parser());
}

Ref<GDScriptParserRef> GDScriptAnalyzer::find_cached_external_parser_for_class(const GDScriptParser::ClassNode *p_class, GDScriptParser *p_dependant_parser) {
	if (p_dependant_parser == nullptr) {
		return nullptr;
	}

	String script_path = p_class->get_datatype().script_path;
	if (HashMap<String, Ref<GDScriptParserRef>>::Iterator E = p_dependant_parser->depended_parsers.find(script_path)) {
		if (E->value.is_valid()) {
			// Silently ensure it's parsed.
			E->value->raise_status(GDScriptParserRef::PARSED);
			if (E->value->get_parser()->has_class(p_class)) {
				return E->value;
			}
		}
	}

	return nullptr;
}

Ref<GDScript> GDScriptAnalyzer::get_depended_shallow_script(const String &p_path, Error &r_error) {
	// To keep a local cache of the parser for resolving external nodes later.
	const String path = ResourceUID::ensure_path(p_path);
	parser->get_depended_parser_for(path);
	Ref<GDScript> scr = GDScriptCache::get_shallow_script(path, r_error, parser->script_path);
	return scr;
}

void GDScriptAnalyzer::reduce_identifier_from_base_set_class(GDScriptParser::IdentifierNode *p_identifier, GDScriptParser::DataType p_identifier_datatype) {
	ERR_FAIL_NULL(p_identifier);

	p_identifier->set_datatype(p_identifier_datatype);
	if (p_identifier_datatype.class_type != nullptr && p_identifier_datatype.class_type->is_trait) {
		return;
	}
	Error err = OK;
	Ref<GDScript> scr = get_depended_shallow_script(p_identifier_datatype.script_path, err);
	if (err) {
		push_error(vformat(R"(Error while getting cache for script "%s".)", p_identifier_datatype.script_path), p_identifier);
		return;
	}
	p_identifier->reduced_value = scr->find_class(p_identifier_datatype.class_type->fqcn);
	p_identifier->is_constant = true;
}

void GDScriptAnalyzer::reduce_identifier_from_base(GDScriptParser::IdentifierNode *p_identifier, GDScriptParser::DataType *p_base) {
	if (!p_identifier->get_datatype().has_no_type()) {
		return;
	}

	GDScriptParser::DataType base;
	if (p_base == nullptr) {
		base = type_from_metatype(parser->current_class->get_datatype());
	} else {
		base = *p_base;
	}

	// A value of a constrained type parameter `[T: Bound]` exposes the members of its bound,
	// so member access on `T` is resolved against `Bound`.
	if (base.kind == GDScriptParser::DataType::TYPE_PARAMETER && !base.type_parameter_bound.is_empty()) {
		const bool was_meta_type = base.is_meta_type;
		base = base.type_parameter_bound[0];
		base.is_meta_type = was_meta_type;
	}

	StringName name = p_identifier->name;

	if (base.kind == GDScriptParser::DataType::ENUM) {
		if (base.is_meta_type) {
			if (base.enum_values.has(name)) {
				p_identifier->set_datatype(type_from_metatype(base));
				p_identifier->is_constant = true;
				p_identifier->reduced_value = base.enum_values[name];
				return;
			}

			// Enum does not have this value, return.
			return;
		} else {
			push_error(R"(Cannot get property from enum value.)", p_identifier);
			return;
		}
	}

	if (base.kind == GDScriptParser::DataType::BUILTIN) {
		if (base.is_meta_type) {
			bool valid = false;

			if (Variant::has_constant(base.builtin_type, name)) {
				valid = true;

				const Variant constant_value = Variant::get_constant_value(base.builtin_type, name);

				p_identifier->is_constant = true;
				p_identifier->reduced_value = constant_value;
				p_identifier->set_datatype(type_from_variant(constant_value, p_identifier));
			}

			if (!valid) {
				const StringName enum_name = Variant::get_enum_for_enumeration(base.builtin_type, name);
				if (enum_name != StringName()) {
					valid = true;

					p_identifier->is_constant = true;
					p_identifier->reduced_value = Variant::get_enum_value(base.builtin_type, enum_name, name);
					p_identifier->set_datatype(make_builtin_enum_type(enum_name, base.builtin_type, false));
				}
			}

			if (!valid && Variant::has_enum(base.builtin_type, name)) {
				valid = true;

				p_identifier->set_datatype(make_builtin_enum_type(name, base.builtin_type, true));
			}

			if (!valid && base.is_hard_type()) {
#ifdef SUGGEST_GODOT4_RENAMES
				String rename_hint;
				if (GLOBAL_GET_CACHED(bool, "debug/gdscript/warnings/renamed_in_godot_4_hint")) {
					const char *renamed_identifier_name = check_for_renamed_identifier(name, p_identifier->type);
					if (renamed_identifier_name) {
						rename_hint = " " + vformat(R"(Did you mean to use "%s"?)", renamed_identifier_name);
					}
				}
				push_error(vformat(R"(Cannot find member "%s" in base "%s".%s)", name, base.to_string(), rename_hint), p_identifier);
#else
				push_error(vformat(R"(Cannot find member "%s" in base "%s".)", name, base.to_string()), p_identifier);
#endif // SUGGEST_GODOT4_RENAMES
			}
		} else {
			switch (base.builtin_type) {
				case Variant::NIL: {
					if (base.is_hard_type()) {
						push_error(vformat(R"(Cannot get property "%s" on a null object.)", name), p_identifier);
					}
					return;
				}
				case Variant::DICTIONARY: {
					GDScriptParser::DataType dummy;
					dummy.kind = GDScriptParser::DataType::VARIANT;
					p_identifier->set_datatype(dummy);
					return;
				}
				default: {
					Callable::CallError temp;
					Variant dummy;
					Variant::construct(base.builtin_type, dummy, nullptr, 0, temp);
					List<PropertyInfo> properties;
					dummy.get_property_list(&properties);
					for (const PropertyInfo &prop : properties) {
						if (prop.name == name) {
							p_identifier->set_datatype(type_from_property(prop));
							return;
						}
					}
					if (Variant::has_builtin_method(base.builtin_type, name)) {
						p_identifier->set_datatype(explicit_callable_type_from_info(Variant::get_builtin_method_info(base.builtin_type, name)));
						return;
					}
					if (base.is_hard_type()) {
#ifdef SUGGEST_GODOT4_RENAMES
						String rename_hint;
						if (GLOBAL_GET_CACHED(bool, "debug/gdscript/warnings/renamed_in_godot_4_hint")) {
							const char *renamed_identifier_name = check_for_renamed_identifier(name, p_identifier->type);
							if (renamed_identifier_name) {
								rename_hint = " " + vformat(R"(Did you mean to use "%s"?)", renamed_identifier_name);
							}
						}
						push_error(vformat(R"(Cannot find member "%s" in base "%s".%s)", name, base.to_string(), rename_hint), p_identifier);
#else
						push_error(vformat(R"(Cannot find member "%s" in base "%s".)", name, base.to_string()), p_identifier);
#endif // SUGGEST_GODOT4_RENAMES
					}
				}
			}
		}
		return;
	}

	GDScriptParser::ClassNode *base_class = base.class_type;
	List<GDScriptParser::ClassNode *> script_classes;
	HashSet<GDScriptParser::ClassNode *> trait_interface_classes;
	bool is_base = true;

	if (base_class != nullptr) {
		get_class_node_current_scope_classes(base_class, &script_classes, p_identifier);
		// Flattened trait members are reachable from a class that applies the trait
		// (directly or transitively), and from a trait that requires another trait.
		// They are treated as instance-accessible members of the using scope.
		if (base_class->is_trait || !base_class->used_traits.is_empty()) {
			resolve_trait_uses(base_class, p_identifier);
			for (GDScriptParser::ClassNode *trait : base_class->resolved_traits) {
				if (script_classes.find(trait) == nullptr) {
					script_classes.push_back(trait);
				}
				trait_interface_classes.insert(trait);
			}
		}
	}

	bool is_constructor = base.is_meta_type && p_identifier->name == SNAME("new");

	for (GDScriptParser::ClassNode *script_class : script_classes) {
		const bool is_trait_interface_class = trait_interface_classes.has(script_class);
		const bool can_access_instance_member = is_base || is_trait_interface_class;

		if (p_base == nullptr && script_class->identifier && script_class->identifier->name == name) {
			reduce_identifier_from_base_set_class(p_identifier, script_class->get_datatype());
			if (script_class->outer != nullptr) {
				p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CLASS;
			}
			return;
		}

		if (is_constructor) {
			name = "_init";
		}

		if (script_class->has_member(name)) {
			resolve_class_member(script_class, name, p_identifier);

			GDScriptParser::ClassNode::Member member = script_class->get_member(name);
			switch (member.type) {
				case GDScriptParser::ClassNode::Member::CONSTANT: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.constant->initializer->reduced_value;
					p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;
					p_identifier->constant_source = member.constant;
					return;
				}

				case GDScriptParser::ClassNode::Member::ENUM_VALUE: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.enum_value.value;
					p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}

				case GDScriptParser::ClassNode::Member::ENUM: {
					p_identifier->set_datatype(member.get_datatype());
					p_identifier->is_constant = true;
					p_identifier->reduced_value = member.m_enum->dictionary;
					p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;
					return;
				}

				case GDScriptParser::ClassNode::Member::VARIABLE: {
					if (can_access_instance_member && (!base.is_meta_type || member.variable->is_static)) {
						p_identifier->set_datatype(substitute_member_type(member.get_datatype(), specialize_ancestor_type(base, script_class)));
						p_identifier->source = member.variable->is_static ? GDScriptParser::IdentifierNode::STATIC_VARIABLE : GDScriptParser::IdentifierNode::MEMBER_VARIABLE;
						p_identifier->variable_source = member.variable;
						member.variable->usages += 1;
						return;
					}
				} break;

				case GDScriptParser::ClassNode::Member::SIGNAL: {
					if (can_access_instance_member && !base.is_meta_type) {
						p_identifier->set_datatype(p_base == nullptr ? member.get_datatype() : explicit_signal_type_from_node(member.signal));
						p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_SIGNAL;
						p_identifier->signal_source = member.signal;
						member.signal->usages += 1;
						return;
					}
				} break;

				case GDScriptParser::ClassNode::Member::FUNCTION: {
					if (can_access_instance_member && (!base.is_meta_type || member.function->is_static || is_constructor)) {
						GDScriptParser::DataType callable_type = make_callable_type(member.function->info, member.function);
						// Substitute the method's `T`-typed parameters and return through the inheritance
						// chain, so `IntList extends List[int]` sees `func get() -> T` as `-> int`. The
						// method's own type parameters shadow same-named class ones and are left intact.
						callable_type = substitute_member_type(callable_type, specialize_ancestor_type(base, script_class), member.function);
						if (p_base != nullptr) {
							callable_type.has_explicit_method_signature = true;
						}
						p_identifier->set_datatype(callable_type);
						p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_FUNCTION;
						p_identifier->function_source = member.function;
						p_identifier->function_source_is_static = member.function->is_static;
						return;
					}
				} break;

				case GDScriptParser::ClassNode::Member::CLASS: {
					reduce_identifier_from_base_set_class(p_identifier, member.get_datatype());
					p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CLASS;
					return;
				}

				default: {
					// Do nothing
				}
			}
		}

		if (is_base) {
			is_base = script_class->base_type.class_type != nullptr;
			if (!is_base && p_base != nullptr && trait_interface_classes.is_empty()) {
				break;
			}
		}
	}

	// Check non-GDScript scripts.
	Ref<Script> script_type = base.script_type;

	if (base_class == nullptr && script_type.is_valid()) {
		List<PropertyInfo> property_list;
		script_type->get_script_property_list(&property_list);

		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name != p_identifier->name) {
				continue;
			}

			const GDScriptParser::DataType property_type = GDScriptAnalyzer::type_from_property(property_info, false, false);

			p_identifier->set_datatype(property_type);
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_VARIABLE;
			return;
		}

		MethodInfo method_info = script_type->get_method_info(p_identifier->name);

		if (method_info.name == p_identifier->name) {
			p_identifier->set_datatype(explicit_callable_type_from_info(method_info));
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_FUNCTION;
			p_identifier->function_source_is_static = method_info.flags & METHOD_FLAG_STATIC;
			return;
		}

		List<MethodInfo> signal_list;
		script_type->get_script_signal_list(&signal_list);

		for (const MethodInfo &signal_info : signal_list) {
			if (signal_info.name != p_identifier->name) {
				continue;
			}

			// Reconstruct the full signature (routing each argument through type_from_property) so a
			// directly-accessed external signal member keeps its parameter types — and any nested
			// callable/signal hint — for emit()/connect() compatibility checks. Plain make_signal_type
			// would leave the signature empty and erase it back to untyped at the script-API boundary.
			GDScriptParser::DataType signal_type = explicit_signal_type_from_info(signal_info);
			if (!_signature_is_comparison_safe(signal_type.method_parameter_types)) {
				// A user-class/enum slot rebuilt from PropertyInfo cannot be compared reliably as a rich
				// explicit signature (it surfaces as SCRIPT while a local annotation may be a CLASS handle).
				// Fall back to the MethodInfo form so compatibility is decided by class name instead.
				signal_type = make_signal_type(signal_info);
			}

			p_identifier->set_datatype(signal_type);
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_SIGNAL;
			return;
		}

		HashMap<StringName, Variant> constant_map;
		script_type->get_constants(&constant_map);

		if (constant_map.has(p_identifier->name)) {
			Variant constant = constant_map.get(p_identifier->name);

			p_identifier->set_datatype(make_builtin_meta_type(constant.get_type()));
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;
			return;
		}
	}

	// Check native members. No need for native class recursion because Node exposes all Object's properties.
	const StringName &native = base.native_type;

	if (class_exists(native)) {
		if (is_constructor) {
			name = "_init";
		}

		MethodInfo method_info;
		if (ClassDB::has_property(native, name)) {
			StringName getter_name = ClassDB::get_property_getter(native, name);
			MethodBind *getter = ClassDB::get_method(native, getter_name);
			if (getter != nullptr) {
				bool has_setter = ClassDB::get_property_setter(native, name) != StringName();
				p_identifier->set_datatype(type_from_property(getter->get_return_info(), false, !has_setter));
				p_identifier->source = GDScriptParser::IdentifierNode::INHERITED_VARIABLE;
			}
			return;
		}
		if (ClassDB::get_method_info(native, name, &method_info)) {
			// Method is callable.
			p_identifier->set_datatype(explicit_callable_type_from_info(method_info));
			p_identifier->source = GDScriptParser::IdentifierNode::INHERITED_VARIABLE;
			return;
		}
		if (ClassDB::get_signal(native, name, &method_info)) {
			// Signal is a type too.
			p_identifier->set_datatype(explicit_signal_type_from_info(method_info));
			p_identifier->source = GDScriptParser::IdentifierNode::INHERITED_VARIABLE;
			return;
		}
		if (ClassDB::has_enum(native, name)) {
			p_identifier->set_datatype(make_native_enum_type(name, native));
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;
			return;
		}
		bool valid = false;

		int64_t int_constant = ClassDB::get_integer_constant(native, name, &valid);
		if (valid) {
			p_identifier->is_constant = true;
			p_identifier->reduced_value = int_constant;
			p_identifier->source = GDScriptParser::IdentifierNode::MEMBER_CONSTANT;

			// Check whether this constant, which exists, belongs to an enum
			StringName enum_name = ClassDB::get_integer_constant_enum(native, name);
			if (enum_name != StringName()) {
				p_identifier->set_datatype(make_native_enum_type(enum_name, native, false));
			} else {
				p_identifier->set_datatype(type_from_variant(int_constant, p_identifier));
			}
		}
	}
}

void GDScriptAnalyzer::reduce_identifier(GDScriptParser::IdentifierNode *p_identifier, bool can_be_builtin) {
	// TODO: This is an opportunity to further infer types.

	// Check if we are inside an enum. This allows enum values to access other elements of the same enum.
	if (current_enum) {
		for (int i = 0; i < current_enum->values.size(); i++) {
			const GDScriptParser::EnumNode::Value &element = current_enum->values[i];
			if (element.identifier->name == p_identifier->name) {
				StringName enum_name = current_enum->identifier ? current_enum->identifier->name : UNNAMED_ENUM;
				GDScriptParser::DataType type = make_class_enum_type(enum_name, parser->current_class, parser->script_path, false);
				if (element.parent_enum->identifier) {
					type.enum_type = element.parent_enum->identifier->name;
				}
				p_identifier->set_datatype(type);

				if (element.resolved) {
					p_identifier->is_constant = true;
					p_identifier->reduced_value = element.value;
				} else {
					push_error(R"(Cannot use another enum element before it was declared.)", p_identifier);
				}
				return; // Found anyway.
			}
		}
	}

	bool found_source = false;
	// Check if identifier is local.
	// If that's the case, the declaration already was solved before.
	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			p_identifier->set_datatype(p_identifier->parameter_source->get_datatype());
			found_source = true;
			break;
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
		case GDScriptParser::IdentifierNode::MEMBER_CONSTANT:
			p_identifier->set_datatype(p_identifier->constant_source->get_datatype());
			p_identifier->is_constant = true;
			// TODO: Constant should have a value on the node itself.
			p_identifier->reduced_value = p_identifier->constant_source->initializer->reduced_value;
			found_source = true;
			break;
		case GDScriptParser::IdentifierNode::MEMBER_SIGNAL:
			p_identifier->signal_source->usages++;
			[[fallthrough]];
		case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
			mark_lambda_use_self();
			break;
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
			mark_lambda_use_self();
			p_identifier->variable_source->usages++;
			[[fallthrough]];
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE:
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			p_identifier->set_datatype(p_identifier->variable_source->get_datatype());
			found_source = true;
#ifdef DEBUG_ENABLED
			if (p_identifier->variable_source && p_identifier->variable_source->assignments == 0 && !(p_identifier->get_datatype().is_hard_type() && p_identifier->get_datatype().kind == GDScriptParser::DataType::BUILTIN)) {
				parser->push_warning(p_identifier, GDScriptWarning::UNASSIGNED_VARIABLE, p_identifier->name);
			}
#endif // DEBUG_ENABLED
			break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
			p_identifier->set_datatype(p_identifier->bind_source->get_datatype());
			found_source = true;
			break;
		case GDScriptParser::IdentifierNode::LOCAL_BIND: {
			GDScriptParser::DataType result = p_identifier->bind_source->get_datatype();
			result.is_constant = true;
			p_identifier->set_datatype(result);
			found_source = true;
		} break;
		case GDScriptParser::IdentifierNode::UNDEFINED_SOURCE:
		case GDScriptParser::IdentifierNode::MEMBER_FUNCTION:
		case GDScriptParser::IdentifierNode::MEMBER_CLASS:
		case GDScriptParser::IdentifierNode::NATIVE_CLASS:
			break;
	}

	if (found_source) {
		const GDScriptParser::Node *flow_key = flow_narrowing_key_from_identifier(p_identifier);
		if (flow_key != nullptr) {
			if (HashMap<const GDScriptParser::Node *, GDScriptParser::DataType>::Iterator E = flow_narrowed_types.find(flow_key)) {
				p_identifier->set_datatype(E->value);
			}
		}
	}

#ifdef DEBUG_ENABLED
	if (!found_source && p_identifier->suite != nullptr && p_identifier->suite->has_local(p_identifier->name)) {
		parser->push_warning(p_identifier, GDScriptWarning::CONFUSABLE_LOCAL_USAGE, p_identifier->name);
	}
#endif // DEBUG_ENABLED

	// Not a local, so check members.

	if (!found_source) {
		reduce_identifier_from_base(p_identifier);
		if (p_identifier->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE || p_identifier->get_datatype().is_set()) {
			// Found.
			found_source = true;
		}
	}

	if (found_source) {
		const bool source_is_instance_variable = p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_VARIABLE || p_identifier->source == GDScriptParser::IdentifierNode::INHERITED_VARIABLE;
		const bool source_is_instance_function = p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_FUNCTION && !p_identifier->function_source_is_static;
		const bool source_is_signal = p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_SIGNAL;

		if (static_context && (source_is_instance_variable || source_is_instance_function || source_is_signal)) {
			// Get the parent function above any lambda.
			GDScriptParser::FunctionNode *parent_function = parser->current_function;
			while (parent_function && parent_function->source_lambda) {
				parent_function = parent_function->source_lambda->parent_function;
			}

			String source_type;
			if (source_is_instance_variable) {
				source_type = "non-static variable";
			} else if (source_is_instance_function) {
				source_type = "non-static function";
			} else { // source_is_signal
				source_type = "signal";
			}

			if (parent_function) {
				push_error(vformat(R"*(Cannot access %s "%s" from the static function "%s()".)*", source_type, p_identifier->name, parent_function->identifier->name), p_identifier);
			} else {
				push_error(vformat(R"*(Cannot access %s "%s" from a static variable initializer.)*", source_type, p_identifier->name), p_identifier);
			}
		}

		if (current_lambda != nullptr) {
			// If the identifier is a member variable (including the native class properties), member function, or a signal,
			// we consider the lambda to be using `self`, so we keep a reference to the current instance.
			if (source_is_instance_variable || source_is_instance_function || source_is_signal) {
				mark_lambda_use_self();
				return; // No need to capture.
			}

			switch (p_identifier->source) {
				case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
				case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
				case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
				case GDScriptParser::IdentifierNode::LOCAL_BIND:
					break; // Need to capture.
				case GDScriptParser::IdentifierNode::UNDEFINED_SOURCE: // A global.
				case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
				case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
				case GDScriptParser::IdentifierNode::MEMBER_CONSTANT:
				case GDScriptParser::IdentifierNode::MEMBER_FUNCTION:
				case GDScriptParser::IdentifierNode::MEMBER_SIGNAL:
				case GDScriptParser::IdentifierNode::MEMBER_CLASS:
				case GDScriptParser::IdentifierNode::INHERITED_VARIABLE:
				case GDScriptParser::IdentifierNode::STATIC_VARIABLE:
				case GDScriptParser::IdentifierNode::NATIVE_CLASS:
					return; // No need to capture.
			}

			GDScriptParser::FunctionNode *function_test = current_lambda->function;
			// Make sure we aren't capturing variable in the same lambda.
			// This also add captures for nested lambdas.
			while (function_test != nullptr && function_test != p_identifier->source_function && function_test->source_lambda != nullptr && !function_test->source_lambda->captures_indices.has(p_identifier->name)) {
				function_test->source_lambda->captures_indices[p_identifier->name] = function_test->source_lambda->captures.size();
				function_test->source_lambda->captures.push_back(p_identifier);
				mark_flow_narrowing_capture(p_identifier);
				function_test = function_test->source_lambda->parent_function;
			}
		}

		return;
	}

	StringName name = p_identifier->name;
	p_identifier->source = GDScriptParser::IdentifierNode::UNDEFINED_SOURCE;

	// Not a local or a member, so check globals.

	Variant::Type builtin_type = GDScriptParser::get_builtin_type(name);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (can_be_builtin) {
			p_identifier->set_datatype(make_builtin_meta_type(builtin_type));
			return;
		} else {
			push_error(R"(Builtin type cannot be used as a name on its own.)", p_identifier);
		}
	}

	if (class_exists(name)) {
		p_identifier->source = GDScriptParser::IdentifierNode::NATIVE_CLASS;
		p_identifier->set_datatype(make_native_meta_type(name));
		return;
	}

	StringName namespace_global_class;
	bool namespace_error = false;
	if (get_global_class_in_namespace(parser->head->namespace_name, name, namespace_global_class) ||
			get_imported_global_class(name, p_identifier, namespace_global_class, namespace_error)) {
		if (namespace_error) {
			GDScriptParser::DataType dummy;
			dummy.kind = GDScriptParser::DataType::VARIANT;
			p_identifier->set_datatype(dummy);
			return;
		}
		p_identifier->set_datatype(make_global_class_meta_type(namespace_global_class, p_identifier));
		return;
	}

	if (ScriptServer::is_global_class(name)) {
		p_identifier->set_datatype(make_global_class_meta_type(name, p_identifier));
		return;
	}

	// Try singletons.
	// Do this before globals because this might be a singleton loading another one before it's compiled.
	// A language-reserved named global (e.g. the `godot` reflection namespace) wins over a
	// project autoload of the same name, so resolution falls through to the named-global
	// constant below and `godot.reflection` stays reachable even if project.godot defines a
	// shadowing autoload.
	if (ProjectSettings::get_singleton()->has_autoload(name) && !GDScriptLanguage::get_singleton()->is_reserved_global_name(name)) {
		const ProjectSettings::AutoloadInfo &autoload = ProjectSettings::get_singleton()->get_autoload(name);
		if (autoload.is_singleton) {
			// Singleton exists, so it's at least a Node.
			GDScriptParser::DataType result;
			result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
			result.kind = GDScriptParser::DataType::NATIVE;
			result.builtin_type = Variant::OBJECT;
			result.native_type = SNAME("Node");
			if (ResourceLoader::get_resource_type(autoload.path) == "GDScript") {
				Ref<GDScriptParserRef> single_parser = parser->get_depended_parser_for(autoload.path);
				if (single_parser.is_valid()) {
					Error err = single_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
					if (err == OK) {
						result = type_from_metatype(single_parser->get_parser()->head->get_datatype());
					}
				}
			} else if (ResourceLoader::get_resource_type(autoload.path) == "PackedScene") {
				if (GDScriptLanguage::get_singleton()->has_any_global_constant(name)) {
					Variant constant = GDScriptLanguage::get_singleton()->get_any_global_constant(name);
					Node *node = Object::cast_to<Node>(constant);
					if (node != nullptr) {
						Ref<GDScript> scr = node->get_script();
						if (scr.is_valid()) {
							Ref<GDScriptParserRef> single_parser = parser->get_depended_parser_for(scr->get_script_path());
							if (single_parser.is_valid()) {
								Error err = single_parser->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
								if (err == OK) {
									result = type_from_metatype(single_parser->get_parser()->head->get_datatype());
								}
							}
						}
					}
				}
			}
			result.is_constant = true;
			p_identifier->set_datatype(result);
			return;
		}
	}

	if (CoreConstants::is_global_constant(name)) {
		int index = CoreConstants::get_global_constant_index(name);
		StringName enum_name = CoreConstants::get_global_constant_enum(index);
		int64_t value = CoreConstants::get_global_constant_value(index);
		if (enum_name != StringName()) {
			p_identifier->set_datatype(make_global_enum_type(enum_name, StringName(), false));
		} else {
			p_identifier->set_datatype(type_from_variant(value, p_identifier));
		}
		p_identifier->is_constant = true;
		p_identifier->reduced_value = value;
		return;
	}

	if (GDScriptLanguage::get_singleton()->has_any_global_constant(name)) {
		Variant constant = GDScriptLanguage::get_singleton()->get_any_global_constant(name);
		p_identifier->set_datatype(type_from_variant(constant, p_identifier));
		p_identifier->is_constant = true;
		p_identifier->reduced_value = constant;
		return;
	}

	if (CoreConstants::is_global_enum(name)) {
		p_identifier->set_datatype(make_global_enum_type(name, StringName(), true));
		if (!can_be_builtin) {
			push_error(vformat(R"(Global enum "%s" cannot be used on its own.)", name), p_identifier);
		}
		return;
	}

	if (Variant::has_utility_function(name) || GDScriptUtilityFunctions::function_exists(name)) {
		p_identifier->is_constant = true;
		p_identifier->reduced_value = Callable(memnew(GDScriptUtilityCallable(name)));
		MethodInfo method_info;
		if (GDScriptUtilityFunctions::function_exists(name)) {
			method_info = GDScriptUtilityFunctions::get_function_info(name);
		} else {
			method_info = Variant::get_utility_function_info(name);
		}
		p_identifier->set_datatype(make_callable_type(method_info));
		return;
	}

	// Allow "Variant" here since it might be used for nested enums.
	if (can_be_builtin && name == SNAME("Variant")) {
		GDScriptParser::DataType variant;
		variant.kind = GDScriptParser::DataType::VARIANT;
		variant.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		variant.is_meta_type = true;
		variant.is_pseudo_type = true;
		p_identifier->set_datatype(variant);
		return;
	}

	// Not found.
#ifdef SUGGEST_GODOT4_RENAMES
	String rename_hint;
	if (GLOBAL_GET_CACHED(bool, "debug/gdscript/warnings/renamed_in_godot_4_hint")) {
		const char *renamed_identifier_name = check_for_renamed_identifier(name, p_identifier->type);
		if (renamed_identifier_name) {
			rename_hint = " " + vformat(R"(Did you mean to use "%s"?)", renamed_identifier_name);
		}
	}
	push_error(vformat(R"(Identifier "%s" not declared in the current scope.%s)", name, rename_hint), p_identifier);
#else
	push_error(vformat(R"(Identifier "%s" not declared in the current scope.)", name), p_identifier);
#endif // SUGGEST_GODOT4_RENAMES
	GDScriptParser::DataType dummy;
	dummy.kind = GDScriptParser::DataType::VARIANT;
	p_identifier->set_datatype(dummy); // Just so type is set to something.
}

void GDScriptAnalyzer::reduce_lambda(GDScriptParser::LambdaNode *p_lambda) {
	// Lambda is always a Callable.
	GDScriptParser::DataType lambda_type;
	lambda_type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
	lambda_type.kind = GDScriptParser::DataType::BUILTIN;
	lambda_type.builtin_type = Variant::CALLABLE;
	p_lambda->set_datatype(lambda_type);

	if (p_lambda->function == nullptr) {
		return;
	}

	GDScriptParser::LambdaNode *previous_lambda = current_lambda;
	current_lambda = p_lambda;
	resolve_function_signature(p_lambda->function, p_lambda, true);
	current_lambda = previous_lambda;

	lambda_type = make_callable_type(p_lambda->function->info, p_lambda->function);
	lambda_type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
	lambda_type.is_constant = false;
	p_lambda->set_datatype(lambda_type);

	pending_body_resolution_lambdas.push_back(p_lambda);
}

void GDScriptAnalyzer::reduce_literal(GDScriptParser::LiteralNode *p_literal) {
	p_literal->reduced_value = p_literal->value;
	p_literal->is_constant = true;

	p_literal->set_datatype(type_from_variant(p_literal->reduced_value, p_literal));
}

void GDScriptAnalyzer::reduce_preload(GDScriptParser::PreloadNode *p_preload) {
	if (!p_preload->path) {
		return;
	}

	reduce_expression(p_preload->path);

	if (!p_preload->path->is_constant) {
		push_error("Preloaded path must be a constant string.", p_preload->path);
		return;
	}

	if (p_preload->path->reduced_value.get_type() != Variant::STRING) {
		push_error("Preloaded path must be a constant string.", p_preload->path);
	} else {
		p_preload->resolved_path = p_preload->path->reduced_value;
		// TODO: Save this as script dependency.
		if (p_preload->resolved_path.is_relative_path()) {
			p_preload->resolved_path = parser->script_path.get_base_dir().path_join(p_preload->resolved_path);
		}
		p_preload->resolved_path = p_preload->resolved_path.simplify_path();
		if (!ResourceLoader::exists(p_preload->resolved_path)) {
			Ref<FileAccess> file_check = FileAccess::create(FileAccess::ACCESS_RESOURCES);

			if (file_check->file_exists(p_preload->resolved_path)) {
				push_error(vformat(R"(Preload file "%s" has no resource loaders (unrecognized file extension).)", p_preload->resolved_path), p_preload->path);
			} else {
				push_error(vformat(R"(Preload file "%s" does not exist.)", p_preload->resolved_path), p_preload->path);
			}
		} else {
			// TODO: Don't load if validating: use completion cache.

			// Must load GDScript separately to permit cyclic references
			// as ResourceLoader::load() detects and rejects those.
			const String &res_type = ResourceLoader::get_resource_type(p_preload->resolved_path);
			if (res_type == "GDScript") {
				Error err = OK;
				Ref<GDScript> res = get_depended_shallow_script(p_preload->resolved_path, err);
				p_preload->resource = res;
				if (err != OK) {
					push_error(vformat(R"(Could not preload resource script "%s".)", p_preload->resolved_path), p_preload->path);
				}
			} else {
				Error err = OK;
				p_preload->resource = ResourceLoader::load(p_preload->resolved_path, res_type, ResourceFormatLoader::CACHE_MODE_REUSE, &err);
				if (err == ERR_BUSY) {
					p_preload->resource = ResourceLoader::ensure_resource_ref_override_for_outer_load(p_preload->resolved_path, res_type);
				}
				if (p_preload->resource.is_null()) {
					push_error(vformat(R"(Could not preload resource file "%s".)", p_preload->resolved_path), p_preload->path);
				}
			}
		}
	}

	p_preload->is_constant = true;
	p_preload->reduced_value = p_preload->resource;
	p_preload->set_datatype(type_from_variant(p_preload->reduced_value, p_preload));

	// TODO: Not sure if this is necessary anymore.
	// 'type_from_variant()' should call 'resolve_class_inheritance()' which would call 'ensure_cached_external_parser_for_class()'
	// Better safe than sorry.
	ensure_cached_external_parser_for_class(p_preload->get_datatype().class_type, nullptr, "Trying to resolve preload", p_preload);
}

void GDScriptAnalyzer::reduce_self(GDScriptParser::SelfNode *p_self) {
	p_self->is_constant = false;
	p_self->set_datatype(type_from_metatype(parser->current_class->get_datatype()));
	mark_lambda_use_self();
}

void GDScriptAnalyzer::reduce_subscript(GDScriptParser::SubscriptNode *p_subscript, bool p_can_be_pseudo_type) {
	if (p_subscript->base == nullptr) {
		return;
	}
	if (p_subscript->is_attribute && p_subscript->attribute != nullptr) {
		Vector<GDScriptParser::IdentifierNode *> reversed_chain;
		GDScriptParser::ExpressionNode *chain_base = p_subscript;
		while (chain_base != nullptr && chain_base->type == GDScriptParser::Node::SUBSCRIPT) {
			GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(chain_base);
			if (!subscript->is_attribute || subscript->attribute == nullptr) {
				break;
			}
			reversed_chain.push_back(subscript->attribute);
			chain_base = subscript->base;
		}

		if (chain_base != nullptr && chain_base->type == GDScriptParser::Node::IDENTIFIER) {
			GDScriptParser::IdentifierNode *root_identifier = static_cast<GDScriptParser::IdentifierNode *>(chain_base);
			if (root_identifier->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE && !is_namespace_chain_root_shadowed(root_identifier)) {
				Vector<GDScriptParser::IdentifierNode *> type_chain;
				type_chain.push_back(root_identifier);
				for (int i = reversed_chain.size() - 1; i >= 0; i--) {
					type_chain.push_back(reversed_chain[i]);
				}

				StringName namespace_global_class;
				bool namespace_error = false;
				int namespace_type_chain_size = 0;
				if (get_namespace_global_class_from_type_chain(type_chain, p_subscript, namespace_global_class, namespace_type_chain_size, namespace_error)) {
					if (namespace_error) {
						GDScriptParser::DataType dummy;
						dummy.kind = GDScriptParser::DataType::VARIANT;
						p_subscript->set_datatype(dummy);
						return;
					}

					GDScriptParser::DataType namespace_class_type = make_global_class_meta_type(namespace_global_class, p_subscript);
					for (int i = namespace_type_chain_size; i < type_chain.size(); i++) {
						GDScriptParser::DataType base = namespace_class_type;
						reduce_identifier_from_base(type_chain[i], &base);
						namespace_class_type = type_chain[i]->get_datatype();
						if (!namespace_class_type.is_set()) {
							GDScriptParser::DataType dummy;
							dummy.kind = GDScriptParser::DataType::VARIANT;
							p_subscript->set_datatype(dummy);
							return;
						}
					}
					GDScriptParser::IdentifierNode *last_identifier = type_chain[type_chain.size() - 1];
					p_subscript->attribute->set_datatype(namespace_class_type);
					p_subscript->set_datatype(namespace_class_type);
					p_subscript->is_constant = last_identifier->is_constant;
					p_subscript->reduced_value = last_identifier->reduced_value;
					return;
				}
			}
		}
	}
	if (p_subscript->base->type == GDScriptParser::Node::IDENTIFIER) {
		reduce_identifier(static_cast<GDScriptParser::IdentifierNode *>(p_subscript->base), true);
	} else if (p_subscript->base->type == GDScriptParser::Node::SUBSCRIPT) {
		reduce_subscript(static_cast<GDScriptParser::SubscriptNode *>(p_subscript->base), true);
	} else {
		reduce_expression(p_subscript->base);
	}

	GDScriptParser::DataType result_type;

	if (p_subscript->is_attribute) {
		if (p_subscript->attribute == nullptr) {
			return;
		}

		GDScriptParser::DataType base_type = p_subscript->base->get_datatype();
		bool valid = false;

		// If the base is a metatype, use the analyzer instead.
		if (p_subscript->base->is_constant && !base_type.is_meta_type) {
			// GH-92534. If the base is a GDScript, use the analyzer instead.
			bool base_is_gdscript = false;
			if (p_subscript->base->reduced_value.get_type() == Variant::OBJECT) {
				Ref<GDScript> gdscript = Object::cast_to<GDScript>(p_subscript->base->reduced_value.get_validated_object());
				if (gdscript.is_valid()) {
					base_is_gdscript = true;
					// Makes a metatype from a constant GDScript, since `base_type` is not a metatype.
					GDScriptParser::DataType base_type_meta = type_from_variant(gdscript, p_subscript);
					// First try to reduce the attribute from the metatype.
					reduce_identifier_from_base(p_subscript->attribute, &base_type_meta);
					GDScriptParser::DataType attr_type = p_subscript->attribute->get_datatype();
					if (attr_type.is_set()) {
						valid = !attr_type.is_pseudo_type || p_can_be_pseudo_type;
						result_type = attr_type;
						p_subscript->is_constant = p_subscript->attribute->is_constant;
						p_subscript->reduced_value = p_subscript->attribute->reduced_value;
					}
					if (!valid) {
						// If unsuccessful, reset and return to the normal route.
						p_subscript->attribute->set_datatype(GDScriptParser::DataType());
					}
				}
			}
			if (!base_is_gdscript) {
				// Just try to get it.
				Variant value = p_subscript->base->reduced_value.get_named(p_subscript->attribute->name, valid);
				if (valid) {
					p_subscript->is_constant = true;
					p_subscript->reduced_value = value;
					result_type = type_from_variant(value, p_subscript);
				}
			}
		}

		if (valid) {
			// Do nothing.
		} else if (base_type.is_variant() || !base_type.is_hard_type()) {
			valid = !base_type.is_pseudo_type || p_can_be_pseudo_type;
			result_type.kind = GDScriptParser::DataType::VARIANT;
			if (base_type.is_variant() && base_type.is_hard_type() && base_type.is_meta_type && base_type.is_pseudo_type) {
				// Special case: it may be a global enum with pseudo base (e.g. Variant.Type).
				String enum_name;
				if (p_subscript->base->type == GDScriptParser::Node::IDENTIFIER) {
					enum_name = String(static_cast<GDScriptParser::IdentifierNode *>(p_subscript->base)->name) + ENUM_SEPARATOR + String(p_subscript->attribute->name);
				}
				if (CoreConstants::is_global_enum(enum_name)) {
					result_type = make_global_enum_type(enum_name, StringName());
				} else {
					valid = false;
					mark_node_unsafe(p_subscript);
				}
			} else if (strict_dynamic_checks) {
				push_error(vformat(R"*(Cannot resolve member "%s" on type "%s" in strict dynamic mode.)*", p_subscript->attribute->name, base_type.to_string()), p_subscript->attribute);
			} else {
				mark_node_unsafe(p_subscript);
			}
		} else {
			reduce_identifier_from_base(p_subscript->attribute, &base_type);
			GDScriptParser::DataType attr_type = p_subscript->attribute->get_datatype();
			if (attr_type.is_set()) {
				if (base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types()) {
					Variant::Type key_type = base_type.get_container_element_type_or_variant(0).builtin_type;
					valid = key_type == Variant::NIL || key_type == Variant::STRING || key_type == Variant::STRING_NAME;
					if (base_type.has_container_element_type(1)) {
						result_type = base_type.get_container_element_type(1);
						result_type.type_source = base_type.type_source;
					} else {
						result_type.builtin_type = Variant::NIL;
						result_type.kind = GDScriptParser::DataType::VARIANT;
						result_type.type_source = GDScriptParser::DataType::UNDETECTED;
					}
				} else {
					valid = !attr_type.is_pseudo_type || p_can_be_pseudo_type;
					result_type = attr_type;
					p_subscript->is_constant = p_subscript->attribute->is_constant;
					p_subscript->reduced_value = p_subscript->attribute->reduced_value;
				}
			} else if (!base_type.is_meta_type || !base_type.is_constant) {
				valid = base_type.kind != GDScriptParser::DataType::BUILTIN;
				if (valid) {
					if (strict_dynamic_checks) {
						push_error(vformat(R"*(Cannot resolve member "%s" on type "%s" in strict dynamic mode.)*", p_subscript->attribute->name, base_type.to_string()), p_subscript->attribute);
					} else {
#ifdef DEBUG_ENABLED
						parser->push_warning(p_subscript, GDScriptWarning::UNSAFE_PROPERTY_ACCESS, p_subscript->attribute->name, base_type.to_string());
#endif // DEBUG_ENABLED
					}
				}
				result_type.kind = GDScriptParser::DataType::VARIANT;
				mark_node_unsafe(p_subscript);
			}
		}

		if (!valid) {
			GDScriptParser::DataType attr_type = p_subscript->attribute->get_datatype();
			if (!p_can_be_pseudo_type && (attr_type.is_pseudo_type || result_type.is_pseudo_type)) {
				push_error(vformat(R"(Type "%s" in base "%s" cannot be used on its own.)", p_subscript->attribute->name, type_from_metatype(base_type).to_string()), p_subscript->attribute);
			} else {
				push_error(vformat(R"(Cannot find member "%s" in base "%s".)", p_subscript->attribute->name, type_from_metatype(base_type).to_string()), p_subscript->attribute);
			}
			result_type.kind = GDScriptParser::DataType::VARIANT;
		}
	} else {
		if (p_subscript->index == nullptr) {
			return;
		}

		GDScriptParser::DataType base_meta_type = p_subscript->base->get_datatype();
		if (base_meta_type.is_set() && base_meta_type.is_meta_type && base_meta_type.kind == GDScriptParser::DataType::CLASS &&
				base_meta_type.class_type != nullptr && !base_meta_type.class_type->type_parameters.is_empty()) {
			// Generic class specialization in value position, e.g. `Box[int]` or `Pair[int, String]`.
			// The brackets carry a type-argument list rather than an index. A multi-argument list is
			// captured in `type_arguments`; a single argument keeps using `index`.
			GDScriptParser::DataType specialized = base_meta_type;
			Vector<GDScriptParser::DataType> resolved_arguments;
			Vector<bool> argument_failed;
			Vector<const GDScriptParser::Node *> argument_sources;
			Vector<GDScriptParser::ExpressionNode *> argument_expressions;
			if (p_subscript->type_arguments.is_empty()) {
				argument_expressions.push_back(p_subscript->index);
			} else {
				argument_expressions = p_subscript->type_arguments;
			}
			for (GDScriptParser::ExpressionNode *argument_expression : argument_expressions) {
				// Resolve positionally: a failed argument keeps its slot (filled with the Variant
				// fallback and flagged) so the arity check sees the count the user wrote and a later
				// argument is never shifted into an earlier type parameter.
				GDScriptParser::DataType type_argument;
				if (resolve_explicit_type_argument(argument_expression, type_argument)) {
					resolved_arguments.push_back(type_argument);
					argument_failed.push_back(false);
				} else {
					push_error(vformat(R"(Could not resolve the type argument for generic class "%s".)", specialized.to_string()), argument_expression);
					GDScriptParser::DataType fallback;
					fallback.kind = GDScriptParser::DataType::VARIANT;
					fallback.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
					resolved_arguments.push_back(fallback);
					argument_failed.push_back(true);
				}
				argument_sources.push_back(argument_expression);
			}

			const int expected_argument_count = specialized.class_type->type_parameters.size();
			if (resolved_arguments.size() != expected_argument_count) {
				push_error(vformat(R"(Generic class "%s" expects %d type argument(s), but %d were given.)", specialized.to_string(), expected_argument_count, resolved_arguments.size()), p_subscript);
				result_type.kind = GDScriptParser::DataType::VARIANT;
			} else {
				bind_class_type_arguments(specialized, resolved_arguments, argument_failed, argument_sources, p_subscript);
				specialized.is_meta_type = true;
				result_type = specialized;
				// The specialized handle still refers to the same class object at runtime; carry the
				// base's constant value so `Box[int].new()` can recover the script to instantiate.
				p_subscript->is_constant = p_subscript->base->is_constant;
				p_subscript->reduced_value = p_subscript->base->reduced_value;
			}
			p_subscript->set_datatype(result_type);
			return;
		}

		if (!p_subscript->type_arguments.is_empty()) {
			// A comma-separated or `?`-marked type-argument list only makes sense as a generic
			// specialization (handled above) or an explicit generic-method application (handled in
			// call reduction). Reaching here means it was used as an ordinary subscript, which never
			// accepts more than one index.
			push_error(R"(Only a single index is allowed in the subscript operator.)", p_subscript);
			result_type.kind = GDScriptParser::DataType::VARIANT;
			p_subscript->set_datatype(result_type);
			return;
		}

		reduce_expression(p_subscript->index);

		if (p_subscript->base->is_constant && p_subscript->index->is_constant) {
			// Just try to get it.
			bool valid = false;
			// TODO: Check if `p_subscript->base->reduced_value` is GDScript.
			Variant value = p_subscript->base->reduced_value.get(p_subscript->index->reduced_value, &valid);
			if (!valid) {
				push_error(vformat(R"(Cannot get index "%s" from "%s".)", p_subscript->index->reduced_value, p_subscript->base->reduced_value), p_subscript->index);
				result_type.kind = GDScriptParser::DataType::VARIANT;
			} else {
				p_subscript->is_constant = true;
				p_subscript->reduced_value = value;
				result_type = type_from_variant(value, p_subscript);
			}
		} else {
			GDScriptParser::DataType base_type = p_subscript->base->get_datatype();
			GDScriptParser::DataType index_type = p_subscript->index->get_datatype();

			if (base_type.is_variant()) {
				result_type.kind = GDScriptParser::DataType::VARIANT;
				if (strict_dynamic_checks) {
					push_error("Cannot use subscript operator on Variant in strict dynamic mode.", p_subscript->base);
				} else {
					mark_node_unsafe(p_subscript);
				}
			} else {
				if (index_type.is_variant() && strict_dynamic_checks) {
					push_error(vformat(R"*(Cannot use dynamic index of type "%s" for base of type "%s" in strict dynamic mode.)*", index_type.to_string(), base_type.to_string()), p_subscript->index);
				}
				if (base_type.kind == GDScriptParser::DataType::BUILTIN && !index_type.is_variant()) {
					// Check if indexing is valid.
					bool error = index_type.kind != GDScriptParser::DataType::BUILTIN && base_type.builtin_type != Variant::DICTIONARY;
					if (!error) {
						switch (base_type.builtin_type) {
							// Expect int or real as index.
							case Variant::PACKED_BYTE_ARRAY:
							case Variant::PACKED_FLOAT32_ARRAY:
							case Variant::PACKED_FLOAT64_ARRAY:
							case Variant::PACKED_INT32_ARRAY:
							case Variant::PACKED_INT64_ARRAY:
							case Variant::PACKED_STRING_ARRAY:
							case Variant::PACKED_VECTOR2_ARRAY:
							case Variant::PACKED_VECTOR3_ARRAY:
							case Variant::PACKED_COLOR_ARRAY:
							case Variant::PACKED_VECTOR4_ARRAY:
							case Variant::ARRAY:
							case Variant::STRING:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::FLOAT;
								break;
							// Expect String only.
							case Variant::RECT2:
							case Variant::RECT2I:
							case Variant::PLANE:
							case Variant::QUATERNION:
							case Variant::AABB:
							case Variant::OBJECT:
								error = index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Expect String or number.
							case Variant::BASIS:
							case Variant::VECTOR2:
							case Variant::VECTOR2I:
							case Variant::VECTOR3:
							case Variant::VECTOR3I:
							case Variant::VECTOR4:
							case Variant::VECTOR4I:
							case Variant::TRANSFORM2D:
							case Variant::TRANSFORM3D:
							case Variant::PROJECTION:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::FLOAT &&
										index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Expect String or int.
							case Variant::COLOR:
								error = index_type.builtin_type != Variant::INT && index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME;
								break;
							// Don't support indexing, but we will check it later.
							case Variant::RID:
							case Variant::BOOL:
							case Variant::CALLABLE:
							case Variant::FLOAT:
							case Variant::INT:
							case Variant::NIL:
							case Variant::NODE_PATH:
							case Variant::SIGNAL:
							case Variant::STRING_NAME:
								break;
							// Support depends on if the dictionary has a typed key, otherwise anything is valid.
							case Variant::DICTIONARY:
								if (base_type.has_container_element_type(0)) {
									GDScriptParser::DataType key_type = base_type.get_container_element_type(0);
									// A `Dictionary[K, V]` whose key type (transitively) involves a method/class type
									// parameter is erased to an untyped key at runtime, so there is no key metadata to
									// validate statically; indexing it inside a generic body is allowed. Mirrors how
									// `Array[T]` element parameters were erased. The key type must itself be erased — a
									// concretely-keyed dictionary (`Dictionary[int, String]`) indexed by an unrelated
									// type parameter is still checked below.
									if (_signature_type_involves_type_parameter(key_type)) {
										break;
									}
									switch (index_type.builtin_type) {
										// Null value will be treated as an empty object, allow.
										case Variant::NIL:
											error = key_type.builtin_type != Variant::OBJECT;
											break;
										// Objects are parsed for validity in a similar manner to container types.
										case Variant::OBJECT:
											if (key_type.builtin_type == Variant::OBJECT) {
												error = !key_type.can_reference(index_type);
											} else {
												error = key_type.builtin_type != Variant::NIL;
											}
											break;
										// String and StringName interchangeable in this context.
										case Variant::STRING:
										case Variant::STRING_NAME:
											error = key_type.builtin_type != Variant::STRING_NAME && key_type.builtin_type != Variant::STRING;
											break;
										// Ints are valid indices for floats, but not the other way around.
										case Variant::INT:
											error = key_type.builtin_type != Variant::INT && key_type.builtin_type != Variant::FLOAT;
											break;
										// All other cases require the types to match exactly.
										default:
											error = key_type.builtin_type != index_type.builtin_type;
											break;
									}
								}
								break;
							// Here for completeness.
							case Variant::VARIANT_MAX:
								break;
						}

						if (error) {
							push_error(vformat(R"(Invalid index type "%s" for a base of type "%s".)", index_type.to_string(), base_type.to_string()), p_subscript->index);
						}
					}
				} else if (base_type.kind != GDScriptParser::DataType::BUILTIN && !index_type.is_variant()) {
					if (index_type.builtin_type != Variant::STRING && index_type.builtin_type != Variant::STRING_NAME) {
						push_error(vformat(R"(Only "String" or "StringName" can be used as index for type "%s", but received "%s".)", base_type.to_string(), index_type.to_string()), p_subscript->index);
					}
				}

				// Check resulting type if possible.
				result_type.builtin_type = Variant::NIL;
				result_type.kind = GDScriptParser::DataType::BUILTIN;
				result_type.type_source = base_type.is_hard_type() ? GDScriptParser::DataType::ANNOTATED_INFERRED : GDScriptParser::DataType::INFERRED;

				if (base_type.kind != GDScriptParser::DataType::BUILTIN) {
					base_type.builtin_type = Variant::OBJECT;
				}
				switch (base_type.builtin_type) {
					// Can't index at all.
					case Variant::RID:
					case Variant::BOOL:
					case Variant::CALLABLE:
					case Variant::FLOAT:
					case Variant::INT:
					case Variant::NIL:
					case Variant::NODE_PATH:
					case Variant::SIGNAL:
					case Variant::STRING_NAME:
						result_type.kind = GDScriptParser::DataType::VARIANT;
						push_error(vformat(R"(Cannot use subscript operator on a base of type "%s".)", base_type.to_string()), p_subscript->base);
						break;
					// Return int.
					case Variant::PACKED_BYTE_ARRAY:
					case Variant::PACKED_INT32_ARRAY:
					case Variant::PACKED_INT64_ARRAY:
					case Variant::VECTOR2I:
					case Variant::VECTOR3I:
					case Variant::VECTOR4I:
						result_type.builtin_type = Variant::INT;
						break;
					// Return float.
					case Variant::PACKED_FLOAT32_ARRAY:
					case Variant::PACKED_FLOAT64_ARRAY:
					case Variant::VECTOR2:
					case Variant::VECTOR3:
					case Variant::VECTOR4:
					case Variant::QUATERNION:
						result_type.builtin_type = Variant::FLOAT;
						break;
					// Return String.
					case Variant::PACKED_STRING_ARRAY:
					case Variant::STRING:
						result_type.builtin_type = Variant::STRING;
						break;
					// Return Vector2.
					case Variant::PACKED_VECTOR2_ARRAY:
					case Variant::TRANSFORM2D:
					case Variant::RECT2:
						result_type.builtin_type = Variant::VECTOR2;
						break;
					// Return Vector2I.
					case Variant::RECT2I:
						result_type.builtin_type = Variant::VECTOR2I;
						break;
					// Return Vector3.
					case Variant::PACKED_VECTOR3_ARRAY:
					case Variant::AABB:
					case Variant::BASIS:
						result_type.builtin_type = Variant::VECTOR3;
						break;
					// Return Color.
					case Variant::PACKED_COLOR_ARRAY:
						result_type.builtin_type = Variant::COLOR;
						break;
					// Return Vector4.
					case Variant::PACKED_VECTOR4_ARRAY:
						result_type.builtin_type = Variant::VECTOR4;
						break;
					// Depends on the index.
					case Variant::TRANSFORM3D:
					case Variant::PROJECTION:
					case Variant::PLANE:
					case Variant::COLOR:
					case Variant::OBJECT:
						result_type.kind = GDScriptParser::DataType::VARIANT;
						result_type.type_source = GDScriptParser::DataType::UNDETECTED;
						break;
					// Can have an element type.
					case Variant::ARRAY:
						if (base_type.has_container_element_type(0)) {
							result_type = base_type.get_container_element_type(0);
							result_type.type_source = base_type.type_source;
						} else {
							result_type.kind = GDScriptParser::DataType::VARIANT;
							result_type.type_source = GDScriptParser::DataType::UNDETECTED;
						}
						break;
					// Can have two element types, but we only care about the value.
					case Variant::DICTIONARY:
						if (base_type.has_container_element_type(1)) {
							result_type = base_type.get_container_element_type(1);
							result_type.type_source = base_type.type_source;
						} else {
							result_type.kind = GDScriptParser::DataType::VARIANT;
							result_type.type_source = GDScriptParser::DataType::UNDETECTED;
						}
						break;
					// Here for completeness.
					case Variant::VARIANT_MAX:
						break;
				}
			}
		}
	}

	p_subscript->set_datatype(result_type);
}

void GDScriptAnalyzer::reduce_ternary_op(GDScriptParser::TernaryOpNode *p_ternary_op, bool p_is_root) {
	reduce_expression(p_ternary_op->condition);
	reduce_expression(p_ternary_op->true_expr, p_is_root);
	reduce_expression(p_ternary_op->false_expr, p_is_root);

	GDScriptParser::DataType result;

	if (p_ternary_op->condition && p_ternary_op->condition->is_constant && p_ternary_op->true_expr->is_constant && p_ternary_op->false_expr && p_ternary_op->false_expr->is_constant) {
		p_ternary_op->is_constant = true;
		if (p_ternary_op->condition->reduced_value.booleanize()) {
			p_ternary_op->reduced_value = p_ternary_op->true_expr->reduced_value;
		} else {
			p_ternary_op->reduced_value = p_ternary_op->false_expr->reduced_value;
		}
	}

	GDScriptParser::DataType true_type;
	if (p_ternary_op->true_expr) {
		true_type = p_ternary_op->true_expr->get_datatype();
	} else {
		true_type.kind = GDScriptParser::DataType::VARIANT;
	}
	GDScriptParser::DataType false_type;
	if (p_ternary_op->false_expr) {
		false_type = p_ternary_op->false_expr->get_datatype();
	} else {
		false_type.kind = GDScriptParser::DataType::VARIANT;
	}

	if (true_type.is_variant() || false_type.is_variant()) {
		result.kind = GDScriptParser::DataType::VARIANT;
	} else {
		result = true_type;
		if (!is_type_compatible(true_type, false_type)) {
			result = false_type;
			if (!is_type_compatible(false_type, true_type)) {
				result.kind = GDScriptParser::DataType::VARIANT;
#ifdef DEBUG_ENABLED
				parser->push_warning(p_ternary_op, GDScriptWarning::INCOMPATIBLE_TERNARY);
#endif // DEBUG_ENABLED
			}
		}
	}
	result.type_source = true_type.is_hard_type() && false_type.is_hard_type() ? GDScriptParser::DataType::ANNOTATED_INFERRED : GDScriptParser::DataType::INFERRED;

	p_ternary_op->set_datatype(result);
}

void GDScriptAnalyzer::reduce_type_test(GDScriptParser::TypeTestNode *p_type_test) {
	GDScriptParser::DataType result;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = GDScriptParser::DataType::BUILTIN;
	result.builtin_type = Variant::BOOL;
	p_type_test->set_datatype(result);

	if (!p_type_test->operand || !p_type_test->test_type) {
		return;
	}

	reduce_expression(p_type_test->operand);
	GDScriptParser::DataType operand_type = p_type_test->operand->get_datatype();
	GDScriptParser::DataType test_type = type_from_metatype(resolve_datatype(p_type_test->test_type));
	p_type_test->test_datatype = test_type;

	if (!operand_type.is_set() || !test_type.is_set()) {
		return;
	}

	if (p_type_test->operand->is_constant) {
		p_type_test->is_constant = true;
		p_type_test->reduced_value = false;

		if (!is_type_compatible(test_type, operand_type)) {
			push_error(vformat(R"(Expression is of type "%s" so it can't be of type "%s".)", operand_type.to_string(), test_type.to_string()), p_type_test->operand);
		} else {
			// The constant's type can still be resolving during re-entrant member resolution;
			// only fold the test when it is known (`is_type_compatible()` treats unset as compatible anyway).
			const GDScriptParser::DataType value_type = type_from_variant(p_type_test->operand->reduced_value, p_type_test->operand);
			if (value_type.is_set() && is_type_compatible(test_type, value_type)) {
				p_type_test->reduced_value = test_type.builtin_type != Variant::OBJECT || !p_type_test->operand->reduced_value.is_null();
			}
		}

		return;
	}

	if (!is_type_compatible(test_type, operand_type) && !is_type_compatible(operand_type, test_type)) {
		if (operand_type.is_hard_type()) {
			push_error(vformat(R"(Expression is of type "%s" so it can't be of type "%s".)", operand_type.to_string(), test_type.to_string()), p_type_test->operand);
		} else {
			downgrade_node_type_source(p_type_test->operand);
		}
	}
}

void GDScriptAnalyzer::reduce_unary_op(GDScriptParser::UnaryOpNode *p_unary_op) {
	reduce_expression(p_unary_op->operand);

	GDScriptParser::DataType result;

	if (p_unary_op->operand == nullptr) {
		result.kind = GDScriptParser::DataType::VARIANT;
		p_unary_op->set_datatype(result);
		return;
	}

	GDScriptParser::DataType operand_type = p_unary_op->operand->get_datatype();

	if (p_unary_op->operand->is_constant) {
		p_unary_op->is_constant = true;
		p_unary_op->reduced_value = Variant::evaluate(p_unary_op->variant_op, p_unary_op->operand->reduced_value, Variant());
		result = type_from_variant(p_unary_op->reduced_value, p_unary_op);
	}

	if (operand_type.is_variant()) {
		result.kind = GDScriptParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for unary "%s" operator in strict dynamic mode.)*", Variant::get_operator_name(p_unary_op->variant_op)), p_unary_op);
		} else {
			mark_node_unsafe(p_unary_op);
		}
	} else {
		bool valid = false;
		result = get_operation_type(p_unary_op->variant_op, operand_type, valid, p_unary_op);

		if (!valid) {
			push_error(vformat(R"(Invalid operand of type "%s" for unary operator "%s".)", operand_type.to_string(), Variant::get_operator_name(p_unary_op->variant_op)), p_unary_op);
		}
	}

	p_unary_op->set_datatype(result);
}

Variant GDScriptAnalyzer::make_expression_reduced_value(GDScriptParser::ExpressionNode *p_expression, bool &is_reduced) {
	if (p_expression == nullptr) {
		return Variant();
	}

	if (p_expression->is_constant) {
		is_reduced = true;
		return p_expression->reduced_value;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY:
			return make_array_reduced_value(static_cast<GDScriptParser::ArrayNode *>(p_expression), is_reduced);
		case GDScriptParser::Node::DICTIONARY:
			return make_dictionary_reduced_value(static_cast<GDScriptParser::DictionaryNode *>(p_expression), is_reduced);
		case GDScriptParser::Node::SUBSCRIPT:
			return make_subscript_reduced_value(static_cast<GDScriptParser::SubscriptNode *>(p_expression), is_reduced);
		case GDScriptParser::Node::CALL:
			return make_call_reduced_value(static_cast<GDScriptParser::CallNode *>(p_expression), is_reduced);
		default:
			break;
	}

	return Variant();
}

Variant GDScriptAnalyzer::make_array_reduced_value(GDScriptParser::ArrayNode *p_array, bool &is_reduced) {
	Array array = p_array->get_datatype().has_container_element_type(0) ? make_array_from_element_datatype(p_array->get_datatype().get_container_element_type(0)) : Array();

	array.resize(p_array->elements.size());
	for (int i = 0; i < p_array->elements.size(); i++) {
		GDScriptParser::ExpressionNode *element = p_array->elements[i];

		bool is_element_value_reduced = false;
		Variant element_value = make_expression_reduced_value(element, is_element_value_reduced);
		if (!is_element_value_reduced) {
			return Variant();
		}

		array[i] = element_value;
	}

	array.make_read_only();

	is_reduced = true;
	return array;
}

Variant GDScriptAnalyzer::make_dictionary_reduced_value(GDScriptParser::DictionaryNode *p_dictionary, bool &is_reduced) {
	Dictionary dictionary = p_dictionary->get_datatype().has_container_element_types()
			? make_dictionary_from_element_datatype(p_dictionary->get_datatype().get_container_element_type_or_variant(0), p_dictionary->get_datatype().get_container_element_type_or_variant(1))
			: Dictionary();

	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		const GDScriptParser::DictionaryNode::Pair &element = p_dictionary->elements[i];

		bool is_element_key_reduced = false;
		Variant element_key = make_expression_reduced_value(element.key, is_element_key_reduced);
		if (!is_element_key_reduced) {
			return Variant();
		}

		bool is_element_value_reduced = false;
		Variant element_value = make_expression_reduced_value(element.value, is_element_value_reduced);
		if (!is_element_value_reduced) {
			return Variant();
		}

		dictionary[element_key] = element_value;
	}

	dictionary.make_read_only();

	is_reduced = true;
	return dictionary;
}

Variant GDScriptAnalyzer::make_subscript_reduced_value(GDScriptParser::SubscriptNode *p_subscript, bool &is_reduced) {
	if (p_subscript->base == nullptr || p_subscript->index == nullptr) {
		return Variant();
	}

	bool is_base_value_reduced = false;
	Variant base_value = make_expression_reduced_value(p_subscript->base, is_base_value_reduced);
	if (!is_base_value_reduced) {
		return Variant();
	}

	if (p_subscript->is_attribute) {
		bool is_valid = false;
		Variant value = base_value.get_named(p_subscript->attribute->name, is_valid);
		if (is_valid) {
			is_reduced = true;
			return value;
		} else {
			return Variant();
		}
	} else {
		bool is_index_value_reduced = false;
		Variant index_value = make_expression_reduced_value(p_subscript->index, is_index_value_reduced);
		if (!is_index_value_reduced) {
			return Variant();
		}

		bool is_valid = false;
		Variant value = base_value.get(index_value, &is_valid);
		if (is_valid) {
			is_reduced = true;
			return value;
		} else {
			return Variant();
		}
	}
}

Variant GDScriptAnalyzer::make_call_reduced_value(GDScriptParser::CallNode *p_call, bool &is_reduced) {
	if (p_call->get_callee_type() == GDScriptParser::Node::IDENTIFIER) {
		Variant::Type type = Variant::NIL;
		if (p_call->function_name == SNAME("Array")) {
			type = Variant::ARRAY;
		} else if (p_call->function_name == SNAME("Dictionary")) {
			type = Variant::DICTIONARY;
		} else {
			return Variant();
		}

		Vector<Variant> args;
		args.resize(p_call->arguments.size());
		const Variant **argptrs = (const Variant **)alloca(sizeof(const Variant *) * args.size());
		for (int i = 0; i < p_call->arguments.size(); i++) {
			bool is_arg_value_reduced = false;
			Variant arg_value = make_expression_reduced_value(p_call->arguments[i], is_arg_value_reduced);
			if (!is_arg_value_reduced) {
				return Variant();
			}
			args.write[i] = arg_value;
			argptrs[i] = &args[i];
		}

		Variant result;
		Callable::CallError ce;
		Variant::construct(type, result, argptrs, args.size(), ce);
		if (ce.error) {
			push_error(vformat(R"(Failed to construct "%s".)", Variant::get_type_name(type)), p_call);
			return Variant();
		}

		if (type == Variant::ARRAY) {
			Array array = result;
			array.make_read_only();
		} else if (type == Variant::DICTIONARY) {
			Dictionary dictionary = result;
			dictionary.make_read_only();
		}

		is_reduced = true;
		return result;
	}

	return Variant();
}

Array GDScriptAnalyzer::make_array_from_element_datatype(const GDScriptParser::DataType &p_element_datatype, const GDScriptParser::Node *p_source_node) {
	Array array;
	array.set_typed(make_container_type_from_datatype(p_element_datatype, p_source_node));
	return array;
}

ContainerType GDScriptAnalyzer::make_container_type_from_datatype(const GDScriptParser::DataType &p_datatype, const GDScriptParser::Node *p_source_node) {
	ContainerType type;
	type.builtin_type = p_datatype.builtin_type;

	if (p_datatype.builtin_type == Variant::OBJECT) {
		Ref<Script> script_type = p_datatype.script_type;
		if (p_datatype.kind == GDScriptParser::DataType::CLASS && script_type.is_null()) {
			Error err = OK;
			Ref<GDScript> scr = get_depended_shallow_script(p_datatype.script_path, err);
			if (err) {
				push_error(vformat(R"(Error while getting cache for script "%s".)", p_datatype.script_path), p_source_node);
				return type;
			}
			script_type.reference_ptr(scr->find_class(p_datatype.class_type->fqcn));
		}
		type.class_name = p_datatype.native_type;
		type.script = script_type;
	}

	for (int i = 0; i < p_datatype.container_element_types.size(); i++) {
		type.element_types.push_back(make_container_type_from_datatype(p_datatype.container_element_types[i], p_source_node));
	}
	return type;
}

Dictionary GDScriptAnalyzer::make_dictionary_from_element_datatype(const GDScriptParser::DataType &p_key_element_datatype, const GDScriptParser::DataType &p_value_element_datatype, const GDScriptParser::Node *p_source_node) {
	Dictionary dictionary;
	dictionary.set_typed(make_container_type_from_datatype(p_key_element_datatype, p_source_node), make_container_type_from_datatype(p_value_element_datatype, p_source_node));
	return dictionary;
}

Variant GDScriptAnalyzer::make_variable_default_value(GDScriptParser::VariableNode *p_variable) {
	Variant result = Variant();

	if (p_variable->initializer) {
		bool is_initializer_value_reduced = false;
		Variant initializer_value = make_expression_reduced_value(p_variable->initializer, is_initializer_value_reduced);
		if (is_initializer_value_reduced) {
			result = initializer_value;
		}
	} else {
		GDScriptParser::DataType datatype = p_variable->get_datatype();
		if (datatype.is_hard_type()) {
			if (datatype.kind == GDScriptParser::DataType::BUILTIN && datatype.builtin_type != Variant::OBJECT) {
				if (datatype.builtin_type == Variant::ARRAY && datatype.has_container_element_type(0)) {
					result = make_array_from_element_datatype(datatype.get_container_element_type(0));
				} else if (datatype.builtin_type == Variant::DICTIONARY && datatype.has_container_element_types()) {
					GDScriptParser::DataType key = datatype.get_container_element_type_or_variant(0);
					GDScriptParser::DataType value = datatype.get_container_element_type_or_variant(1);
					result = make_dictionary_from_element_datatype(key, value);
				} else {
					VariantInternal::initialize(&result, datatype.builtin_type);
				}
			} else if (datatype.kind == GDScriptParser::DataType::ENUM) {
				result = 0;
			}
		}
	}

	return result;
}

static GDScriptParser::DataType _type_from_container_type(const ContainerType &p_type) {
	GDScriptParser::DataType result;
	result.is_constant = true;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	if (p_type.builtin_type == Variant::NIL) {
		result.kind = GDScriptParser::DataType::VARIANT;
		return result;
	}
	if (p_type.script.is_valid()) {
		result = GDScriptAnalyzer::type_from_metatype(make_script_meta_type(p_type.script));
	} else if (p_type.class_name != StringName()) {
		result = GDScriptAnalyzer::type_from_metatype(make_native_meta_type(p_type.class_name));
	} else {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = p_type.builtin_type;
	}
	result.is_constant = true;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	for (const ContainerType &element_type : p_type.element_types) {
		result.set_container_element_type(result.get_container_element_type_count(), _type_from_container_type(element_type));
	}
	return result;
}

GDScriptParser::DataType GDScriptAnalyzer::type_from_variant(const Variant &p_value, const GDScriptParser::Node *p_source) {
	GDScriptParser::DataType result;
	result.is_constant = true;
	result.kind = GDScriptParser::DataType::BUILTIN;
	result.builtin_type = p_value.get_type();
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT; // Constant has explicit type.

	if (p_value.get_type() == Variant::ARRAY) {
		const Array &array = p_value;
		if (array.is_typed()) {
			result.set_container_element_type(0, _type_from_container_type(array.get_element_type()));
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary &dict = p_value;
		if (dict.is_typed_key()) {
			result.set_container_element_type(0, _type_from_container_type(dict.get_key_type()));
		}
		if (dict.is_typed_value()) {
			result.set_container_element_type(1, _type_from_container_type(dict.get_value_type()));
		}
	} else if (p_value.get_type() == Variant::OBJECT) {
		// Object is treated as a native type, not a builtin type.
		result.kind = GDScriptParser::DataType::NATIVE;

		Object *obj = p_value;
		if (!obj) {
			return GDScriptParser::DataType();
		}
		result.native_type = obj->get_class_name();

		Ref<Script> scr = p_value; // Check if value is a script itself.
		if (scr.is_valid()) {
			result.is_meta_type = true;
		} else {
			result.is_meta_type = false;
			scr = obj->get_script();
		}
		if (scr.is_valid()) {
			Ref<GDScript> gds = scr;
			if (gds.is_valid()) {
				// This might be an inner class, so we want to get the parser for the root.
				// But still get the inner class from that tree.
				String script_path = gds->get_script_path();
				Ref<GDScriptParserRef> ref = parser->get_depended_parser_for(script_path);
				if (ref.is_null()) {
					push_error(vformat(R"(Could not find script "%s".)", script_path), p_source);
					GDScriptParser::DataType error_type;
					error_type.kind = GDScriptParser::DataType::VARIANT;
					return error_type;
				}
				Error err = ref->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
				GDScriptParser::ClassNode *found = nullptr;
				if (err == OK) {
					found = ref->get_parser()->find_class(gds->fully_qualified_name);
					if (found != nullptr) {
						err = resolve_class_inheritance(found, p_source);
					}
				}
				if (err || found == nullptr) {
					push_error(vformat(R"(Could not resolve script "%s".)", script_path), p_source);
					GDScriptParser::DataType error_type;
					error_type.kind = GDScriptParser::DataType::VARIANT;
					return error_type;
				}

				result.kind = GDScriptParser::DataType::CLASS;
				result.native_type = found->get_datatype().native_type;
				result.class_type = found;
				result.script_path = ref->get_parser()->script_path;
			} else {
				result.kind = GDScriptParser::DataType::SCRIPT;
				result.native_type = scr->get_instance_base_type();
				result.script_path = scr->get_path();
			}
			result.script_type = scr;
		} else {
			result.kind = GDScriptParser::DataType::NATIVE;
			if (result.native_type == GDScriptNativeClass::get_class_static()) {
				result.is_meta_type = true;
			}
		}
	}

	return result;
}

GDScriptParser::DataType GDScriptAnalyzer::type_from_metatype(const GDScriptParser::DataType &p_meta_type) {
	GDScriptParser::DataType result = p_meta_type;
	result.is_meta_type = false;
	result.is_pseudo_type = false;
	if (p_meta_type.kind == GDScriptParser::DataType::ENUM) {
		result.builtin_type = Variant::INT;
	} else {
		result.is_constant = false;
	}
	return result;
}

GDScriptParser::DataType GDScriptAnalyzer::type_from_property(const PropertyInfo &p_property, bool p_is_arg, bool p_is_readonly) const {
	GDScriptParser::DataType result;
	result.is_read_only = p_is_readonly;
	result.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	if (p_property.type == Variant::NIL && (p_is_arg || (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT))) {
		// Variant
		result.kind = GDScriptParser::DataType::VARIANT;
		return result;
	}
	result.builtin_type = p_property.type;
	if (p_property.type == Variant::OBJECT) {
		if (ScriptServer::is_global_class(p_property.class_name)) {
			result.kind = GDScriptParser::DataType::SCRIPT;
			result.script_path = ScriptServer::get_global_class_path(p_property.class_name);
			result.native_type = ScriptServer::get_global_class_native_base(p_property.class_name);

			Ref<Script> scr = ResourceLoader::load(ScriptServer::get_global_class_path(p_property.class_name));
			if (scr.is_valid()) {
				result.script_type = scr;
			}
		} else {
			result.kind = GDScriptParser::DataType::NATIVE;
			result.native_type = p_property.class_name == StringName() ? "Object" : p_property.class_name;
		}
	} else {
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = p_property.type;
		if ((p_property.type == Variant::CALLABLE || p_property.type == Variant::SIGNAL) &&
				p_property.hint == PROPERTY_HINT_CALLABLE_TYPE && !p_property.hint_string.is_empty()) {
			const String encoded = (p_property.type == Variant::CALLABLE ? String("Callable") : String("Signal")) + p_property.hint_string;
			const GDScriptParser::DataType decoded = _decode_signature_type(encoded);
			result.has_method_signature = decoded.has_method_signature;
			result.has_explicit_method_signature = decoded.has_explicit_method_signature;
			result.method_parameter_types = decoded.method_parameter_types;
			result.method_return_type = decoded.method_return_type;
			result.method_info = decoded.method_info;
		} else if (p_property.type == Variant::ARRAY && p_property.hint == PROPERTY_HINT_ARRAY_TYPE) {
			// Check element type.
			StringName elem_type_name = p_property.hint_string;
			GDScriptParser::DataType elem_type;
			elem_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

			Variant::Type elem_builtin_type = GDScriptParser::get_builtin_type(elem_type_name);
			if (elem_builtin_type < Variant::VARIANT_MAX) {
				// Builtin type.
				elem_type.kind = GDScriptParser::DataType::BUILTIN;
				elem_type.builtin_type = elem_builtin_type;
			} else if (class_exists(elem_type_name)) {
				elem_type.kind = GDScriptParser::DataType::NATIVE;
				elem_type.builtin_type = Variant::OBJECT;
				elem_type.native_type = elem_type_name;
			} else if (ScriptServer::is_global_class(elem_type_name)) {
				// Just load this as it shouldn't be a GDScript.
				Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(elem_type_name));
				elem_type.kind = GDScriptParser::DataType::SCRIPT;
				elem_type.builtin_type = Variant::OBJECT;
				elem_type.native_type = script->get_instance_base_type();
				elem_type.script_type = script;
			} else {
				ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed array.");
			}
			elem_type.is_constant = false;
			result.set_container_element_type(0, elem_type);
		} else if (p_property.type == Variant::DICTIONARY && p_property.hint == PROPERTY_HINT_DICTIONARY_TYPE) {
			// Check element type.
			StringName key_elem_type_name = p_property.hint_string.get_slicec(';', 0);
			GDScriptParser::DataType key_elem_type;
			key_elem_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

			Variant::Type key_elem_builtin_type = GDScriptParser::get_builtin_type(key_elem_type_name);
			if (key_elem_builtin_type < Variant::VARIANT_MAX) {
				// Builtin type.
				key_elem_type.kind = GDScriptParser::DataType::BUILTIN;
				key_elem_type.builtin_type = key_elem_builtin_type;
			} else if (class_exists(key_elem_type_name)) {
				key_elem_type.kind = GDScriptParser::DataType::NATIVE;
				key_elem_type.builtin_type = Variant::OBJECT;
				key_elem_type.native_type = key_elem_type_name;
			} else if (ScriptServer::is_global_class(key_elem_type_name)) {
				// Just load this as it shouldn't be a GDScript.
				Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(key_elem_type_name));
				key_elem_type.kind = GDScriptParser::DataType::SCRIPT;
				key_elem_type.builtin_type = Variant::OBJECT;
				key_elem_type.native_type = script->get_instance_base_type();
				key_elem_type.script_type = script;
			} else {
				ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed dictionary.");
			}
			key_elem_type.is_constant = false;

			StringName value_elem_type_name = p_property.hint_string.get_slicec(';', 1);
			GDScriptParser::DataType value_elem_type;
			value_elem_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

			Variant::Type value_elem_builtin_type = GDScriptParser::get_builtin_type(value_elem_type_name);
			if (value_elem_builtin_type < Variant::VARIANT_MAX) {
				// Builtin type.
				value_elem_type.kind = GDScriptParser::DataType::BUILTIN;
				value_elem_type.builtin_type = value_elem_builtin_type;
			} else if (class_exists(value_elem_type_name)) {
				value_elem_type.kind = GDScriptParser::DataType::NATIVE;
				value_elem_type.builtin_type = Variant::OBJECT;
				value_elem_type.native_type = value_elem_type_name;
			} else if (ScriptServer::is_global_class(value_elem_type_name)) {
				// Just load this as it shouldn't be a GDScript.
				Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(value_elem_type_name));
				value_elem_type.kind = GDScriptParser::DataType::SCRIPT;
				value_elem_type.builtin_type = Variant::OBJECT;
				value_elem_type.native_type = script->get_instance_base_type();
				value_elem_type.script_type = script;
			} else {
				ERR_FAIL_V_MSG(result, "Could not find element type from property hint of a typed dictionary.");
			}
			value_elem_type.is_constant = false;

			result.set_container_element_type(0, key_elem_type);
			result.set_container_element_type(1, value_elem_type);
		} else if (p_property.type == Variant::INT) {
			// Check if it's enum.
			if ((p_property.usage & PROPERTY_USAGE_CLASS_IS_ENUM) && p_property.class_name != StringName()) {
				if (CoreConstants::is_global_enum(p_property.class_name)) {
					result = make_global_enum_type(p_property.class_name, StringName(), false);
					result.is_constant = false;
				} else {
					Vector<String> names = String(p_property.class_name).split(ENUM_SEPARATOR);
					if (names.size() == 2) {
						result = make_enum_type(names[1], names[0], false);
						result.is_constant = false;
					}
				}
			}
			// PROPERTY_USAGE_CLASS_IS_BITFIELD: BitField[T] isn't supported (yet?), use plain int.
		}
	}
	return result;
}

bool GDScriptAnalyzer::get_function_signature(GDScriptParser::Node *p_source, bool p_is_constructor,
		GDScriptParser::DataType p_base_type, const StringName &p_function,
		GDScriptParser::DataType &r_return_type, List<GDScriptParser::DataType> &r_par_types,
		int &r_default_arg_count, BitField<MethodFlags> &r_method_flags,
		StringName *r_native_class, bool *r_is_noreturn,
		GDScriptParser::FunctionNode **r_found_function) {
	r_method_flags = METHOD_FLAGS_DEFAULT;
	r_default_arg_count = 0;
	if (r_native_class) {
		*r_native_class = StringName();
	}
	if (r_is_noreturn) {
		*r_is_noreturn = false;
	}
	if (r_found_function) {
		*r_found_function = nullptr;
	}
	StringName function_name = p_function;

	// A constrained type parameter `[T: Bound]` exposes the methods of its bound, so calls on a
	// `T`-typed value are resolved against `Bound`.
	if (p_base_type.kind == GDScriptParser::DataType::TYPE_PARAMETER && !p_base_type.type_parameter_bound.is_empty()) {
		const bool was_meta_type = p_base_type.is_meta_type;
		p_base_type = p_base_type.type_parameter_bound[0];
		p_base_type.is_meta_type = was_meta_type;
	}

	bool was_enum = false;
	if (p_base_type.kind == GDScriptParser::DataType::ENUM) {
		was_enum = true;
		if (p_base_type.is_meta_type) {
			// Enum type can be treated as a dictionary value.
			p_base_type.kind = GDScriptParser::DataType::BUILTIN;
			p_base_type.is_meta_type = false;
		} else {
			push_error("Cannot call function on enum value.", p_source);
			return false;
		}
	}

	if (p_base_type.kind == GDScriptParser::DataType::BUILTIN) {
		const GDScriptParser::CallNode *call = p_source != nullptr && p_source->type == GDScriptParser::Node::CALL ? static_cast<const GDScriptParser::CallNode *>(p_source) : nullptr;
		if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.is_meta_type && p_function == SNAME("create")) {
			GDScriptParser::DataType callable_type;
			if (call != nullptr && callable_type_from_constant_method_args(call, 0, 1, callable_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_method_flags.set_flag(METHOD_FLAG_STATIC);
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::NIL, "variant"), true));
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				r_return_type = callable_type;
				return true;
			}
			if (call != nullptr && call->arguments.size() >= 2) {
				validate_strict_callable_method_fallback(call, call->arguments[0]->get_datatype(), 1);
			}
		}

		const bool is_callable_call = p_function == SNAME("call");
		const bool is_callable_callv = p_function == SNAME("callv");
		const bool is_callable_call_deferred = p_function == SNAME("call_deferred");
		const bool is_callable_rpc = p_function == SNAME("rpc");
		const bool is_callable_rpc_id = p_function == SNAME("rpc_id");
		if (p_base_type.builtin_type == Variant::CALLABLE && p_base_type.has_explicit_method_signature) {
			const bool is_callable_vararg = (p_base_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
			auto can_bound_argument_fill_parameter = [&](const GDScriptParser::ExpressionNode *p_argument, const GDScriptParser::DataType &p_parameter_type) -> bool {
				if (p_argument == nullptr) {
					return false;
				}
				if (!p_parameter_type.is_hard_type()) {
					return true;
				}

				GDScriptParser::DataType argument_type = p_argument->get_datatype();
				if (argument_type.is_variant() || !argument_type.is_hard_type()) {
					return false;
				}

				return is_type_compatible(p_parameter_type, argument_type, true);
			};
			auto fixed_vararg_accepts_argument_count = [&](const Vector<const GDScriptParser::ExpressionNode *> &p_bound_arguments, int p_argument_count) -> bool {
				const int fixed_argument_count = p_base_type.method_parameter_types.size();
				const int original_default_arg_count = MIN(p_base_type.method_info.default_arguments.size(), fixed_argument_count);
				const int omitted_argument_count = fixed_argument_count - p_argument_count;
				if (omitted_argument_count <= 0) {
					return true;
				}

				const int bound_filled_count = MIN(omitted_argument_count, p_bound_arguments.size());
				const int default_filled_count = omitted_argument_count - bound_filled_count;
				if (default_filled_count > original_default_arg_count) {
					return false;
				}

				for (int i = 0; i < bound_filled_count; i++) {
					if (!can_bound_argument_fill_parameter(p_bound_arguments[i], p_base_type.method_parameter_types[p_argument_count + i])) {
						return false;
					}
				}

				return true;
			};
			auto fixed_vararg_default_arg_count = [&](const Vector<const GDScriptParser::ExpressionNode *> &p_bound_arguments) -> int {
				const int fixed_argument_count = p_base_type.method_parameter_types.size();

				for (int omitted_argument_count = 1; omitted_argument_count <= fixed_argument_count; omitted_argument_count++) {
					if (!fixed_vararg_accepts_argument_count(p_bound_arguments, fixed_argument_count - omitted_argument_count)) {
						return omitted_argument_count - 1;
					}
				}

				return fixed_argument_count;
			};
			auto preserve_fixed_vararg_callable = [&](const Vector<const GDScriptParser::ExpressionNode *> &p_bound_arguments) -> bool {
				if (!is_callable_vararg) {
					return false;
				}

				const int fixed_argument_count = p_base_type.method_parameter_types.size();
				const int default_arg_count = fixed_vararg_default_arg_count(p_bound_arguments);
				const int continuous_min_argument_count = fixed_argument_count - default_arg_count;
				r_return_type = transformed_callable_type(p_base_type, p_base_type.method_parameter_types, default_arg_count, true);
				for (int argument_count = 0; argument_count < continuous_min_argument_count; argument_count++) {
					if (fixed_vararg_accepts_argument_count(p_bound_arguments, argument_count)) {
						r_return_type.method_extra_allowed_argument_counts.push_back(argument_count);
					}
				}
				return true;
			};

			if (is_callable_call || is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id) {
				r_default_arg_count = p_base_type.method_info.default_arguments.size();
				r_method_flags = METHOD_FLAGS_DEFAULT;
				if (is_callable_vararg) {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
				}
				r_return_type = is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id || p_base_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : p_base_type.method_return_type[0];
				if (is_callable_rpc_id) {
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "peer_id"), true));
				}
				for (const GDScriptParser::DataType &parameter_type : p_base_type.method_parameter_types) {
					r_par_types.push_back(parameter_type);
				}
				return true;
			}

			if (is_callable_callv) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
				r_return_type = p_base_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : p_base_type.method_return_type[0];
				validate_callable_array_literal_args(p_base_type.method_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg, array_literal_argument(call, 0), p_function, p_base_type.method_extra_allowed_argument_counts, p_base_type.method_unbound_argument_count);
				return true;
			}

			if (p_function == SNAME("bind")) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_return_type = plain_callable_type();
				if (!is_callable_vararg && call != nullptr) {
					const int bind_argument_count = call->arguments.size();
					const int callable_argument_count = p_base_type.method_parameter_types.size();
					const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
					const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

					for (int i = 0; i < checked_bind_argument_count; i++) {
						r_par_types.push_back(p_base_type.method_parameter_types[checked_bind_start + i]);
					}

					Vector<GDScriptParser::DataType> remaining_parameter_types;
					const int remaining_argument_count = MAX(callable_argument_count - bind_argument_count, 0);
					for (int i = 0; i < remaining_argument_count; i++) {
						remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
					}

					const int remaining_default_arg_count = MAX(p_base_type.method_info.default_arguments.size() - bind_argument_count, 0);
					r_return_type = transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
				} else {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
					if (call != nullptr) {
						Vector<const GDScriptParser::ExpressionNode *> bound_arguments;
						for (GDScriptParser::ExpressionNode *argument : call->arguments) {
							bound_arguments.push_back(argument);
						}
						preserve_fixed_vararg_callable(bound_arguments);
					}
				}
				return true;
			}

			if (p_function == SNAME("bindv")) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
				r_return_type = plain_callable_type();

				GDScriptParser::ArrayNode *bind_array = array_literal_argument(call, 0);
				if (!is_callable_vararg && bind_array != nullptr) {
					const int bind_argument_count = bind_array->elements.size();
					const int callable_argument_count = p_base_type.method_parameter_types.size();
					const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
					const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

					Vector<GDScriptParser::DataType> bound_parameter_types;
					for (int i = 0; i < checked_bind_argument_count; i++) {
						bound_parameter_types.push_back(p_base_type.method_parameter_types[checked_bind_start + i]);
					}
					validate_callable_array_literal_args(bound_parameter_types, 0, false, bind_array, p_function);

					Vector<GDScriptParser::DataType> remaining_parameter_types;
					const int remaining_argument_count = MAX(callable_argument_count - bind_argument_count, 0);
					for (int i = 0; i < remaining_argument_count; i++) {
						remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
					}

					const int remaining_default_arg_count = MAX(p_base_type.method_info.default_arguments.size() - bind_argument_count, 0);
					r_return_type = transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
				} else if (is_callable_vararg && bind_array != nullptr) {
					Vector<const GDScriptParser::ExpressionNode *> bound_arguments;
					for (GDScriptParser::ExpressionNode *argument : bind_array->elements) {
						bound_arguments.push_back(argument);
					}
					preserve_fixed_vararg_callable(bound_arguments);
				}
				return true;
			}

			if (p_function == SNAME("unbind")) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "argcount"), true));
				r_return_type = plain_callable_type();

				if (call != nullptr && call->arguments.size() == 1) {
					const GDScriptParser::ExpressionNode *unbind_count_arg = call->arguments[0];
					if (unbind_count_arg->is_constant && unbind_count_arg->reduced_value.get_type() == Variant::INT) {
						const int64_t unbind_argument_count = unbind_count_arg->reduced_value;
						if (unbind_argument_count <= 0) {
							push_error("Amount of \"unbind()\" arguments must be 1 or greater.", unbind_count_arg);
						} else {
							const int unbind_count = int(unbind_argument_count);
							Vector<GDScriptParser::DataType> expanded_parameter_types = p_base_type.method_parameter_types;
							const GDScriptParser::DataType variant_type = type_from_property(PropertyInfo(Variant::NIL, ""), true);
							for (int i = 0; i < unbind_count; i++) {
								expanded_parameter_types.push_back(variant_type);
							}
							r_return_type = transformed_callable_type(p_base_type, expanded_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg);
							r_return_type.method_unbound_argument_count = p_base_type.method_unbound_argument_count + unbind_count;
							for (int extra_allowed_argument_count : p_base_type.method_extra_allowed_argument_counts) {
								r_return_type.method_extra_allowed_argument_counts.push_back(extra_allowed_argument_count + unbind_count);
							}
						}
					}
				}
				return true;
			}
		}
		if (p_base_type.builtin_type == Variant::SIGNAL && p_base_type.has_explicit_method_signature && p_function == SNAME("emit")) {
			r_default_arg_count = 0;
			r_method_flags = METHOD_FLAGS_DEFAULT;
			r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
			for (const GDScriptParser::DataType &parameter_type : p_base_type.method_parameter_types) {
				r_par_types.push_back(parameter_type);
			}
			return true;
		}

		if (p_base_type.builtin_type == Variant::ARRAY && p_base_type.has_container_element_type(0)) {
			const bool is_array_single_value_mutation = p_function == SNAME("append") || p_function == SNAME("push_back") ||
					p_function == SNAME("push_front") || p_function == SNAME("fill");
			const bool is_array_indexed_value_mutation = p_function == SNAME("insert") || p_function == SNAME("set");
			const bool is_array_bulk_mutation = p_function == SNAME("assign") || p_function == SNAME("append_array");
			const bool is_array_element_accessor = p_function == SNAME("get") || p_function == SNAME("front") ||
					p_function == SNAME("back") || p_function == SNAME("pick_random") || p_function == SNAME("pop_back") ||
					p_function == SNAME("pop_front") || p_function == SNAME("pop_at");
			const bool is_array_preserved_return = p_function == SNAME("duplicate") || p_function == SNAME("duplicate_deep") || p_function == SNAME("slice");
			if ((is_array_single_value_mutation || is_array_indexed_value_mutation || is_array_bulk_mutation || is_array_element_accessor || is_array_preserved_return) &&
					Variant::has_builtin_method(Variant::ARRAY, p_function)) {
				const MethodInfo method_info = Variant::get_builtin_method_info(Variant::ARRAY, p_function);
				function_signature_from_info(method_info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);

				const GDScriptParser::DataType element_type = p_base_type.get_container_element_type(0);
				GDScriptParser::DataType array_type = type_from_property(PropertyInfo(Variant::ARRAY, "array"), true);
				array_type.set_container_element_type(0, element_type);
				if (is_array_single_value_mutation) {
					r_par_types.clear();
					r_par_types.push_back(element_type);
				} else if (is_array_indexed_value_mutation) {
					r_par_types.clear();
					const StringName index_name = p_function == SNAME("insert") ? SNAME("position") : SNAME("index");
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, index_name), true));
					r_par_types.push_back(element_type);
				} else if (is_array_bulk_mutation) {
					r_par_types.clear();
					r_par_types.push_back(array_type);
				} else if (is_array_element_accessor) {
					r_return_type = element_type;
				} else if (is_array_preserved_return) {
					r_return_type = array_type;
				}
				return true;
			}
		}

		if (p_base_type.builtin_type == Variant::DICTIONARY && p_base_type.has_container_element_types()) {
			const bool is_dictionary_set = p_function == SNAME("set");
			const bool is_dictionary_bulk_mutation = p_function == SNAME("assign") || p_function == SNAME("merge");
			const bool is_dictionary_value_accessor = p_function == SNAME("get") || p_function == SNAME("get_or_add");
			const bool is_dictionary_key_accessor = p_function == SNAME("has") || p_function == SNAME("erase");
			const bool is_dictionary_key_collection_accessor = p_function == SNAME("keys") || p_function == SNAME("has_all");
			const bool is_dictionary_value_collection_accessor = p_function == SNAME("values");
			const bool is_dictionary_value_to_key_accessor = p_function == SNAME("find_key");
			const bool is_dictionary_typed_result = p_function == SNAME("merged") || p_function == SNAME("duplicate") || p_function == SNAME("duplicate_deep");
			if ((is_dictionary_set || is_dictionary_bulk_mutation || is_dictionary_value_accessor || is_dictionary_key_accessor ||
						is_dictionary_key_collection_accessor || is_dictionary_value_collection_accessor || is_dictionary_value_to_key_accessor || is_dictionary_typed_result) &&
					Variant::has_builtin_method(Variant::DICTIONARY, p_function)) {
				const MethodInfo method_info = Variant::get_builtin_method_info(Variant::DICTIONARY, p_function);
				function_signature_from_info(method_info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
				r_par_types.clear();

				const GDScriptParser::DataType key_type = p_base_type.get_container_element_type_or_variant(0);
				const GDScriptParser::DataType value_type = p_base_type.get_container_element_type_or_variant(1);
				GDScriptParser::DataType dictionary_type = type_from_property(PropertyInfo(Variant::DICTIONARY, "dictionary"), true);
				dictionary_type.set_container_element_type(0, key_type);
				dictionary_type.set_container_element_type(1, value_type);
				GDScriptParser::DataType key_array_type = type_from_property(PropertyInfo(Variant::ARRAY, "keys"), true);
				key_array_type.set_container_element_type(0, key_type);

				if (is_dictionary_set) {
					r_par_types.push_back(key_type);
					r_par_types.push_back(value_type);
				} else {
					if (is_dictionary_bulk_mutation || p_function == SNAME("merged")) {
						r_par_types.push_back(dictionary_type);
					}
					if (p_function == SNAME("merge") || p_function == SNAME("merged")) {
						r_par_types.push_back(type_from_property(PropertyInfo(Variant::BOOL, "overwrite"), true));
						if (p_function == SNAME("merged")) {
							r_return_type = dictionary_type;
						}
					} else if (is_dictionary_value_accessor) {
						r_par_types.push_back(key_type);
						r_par_types.push_back(value_type);
						r_return_type = value_type;
					} else if (is_dictionary_key_accessor) {
						r_par_types.push_back(key_type);
					} else if (p_function == SNAME("keys")) {
						r_return_type = key_array_type;
					} else if (p_function == SNAME("has_all")) {
						r_par_types.push_back(key_array_type);
					} else if (p_function == SNAME("values")) {
						GDScriptParser::DataType value_array_type = type_from_property(PropertyInfo(Variant::ARRAY, "values"), true);
						value_array_type.set_container_element_type(0, value_type);
						r_return_type = value_array_type;
					} else if (is_dictionary_value_to_key_accessor) {
						r_par_types.push_back(value_type);
						r_return_type = key_type;
					} else if (is_dictionary_typed_result) {
						for (const PropertyInfo &argument : method_info.arguments) {
							r_par_types.push_back(type_from_property(argument, true));
						}
						r_return_type = dictionary_type;
					}
				}
				return true;
			}
		}

		// Construct a base type to get methods.
		Callable::CallError err;
		Variant dummy;
		Variant::construct(p_base_type.builtin_type, dummy, nullptr, 0, err);
		if (err.error != Callable::CallError::CALL_OK) {
			ERR_FAIL_V_MSG(false, "Could not construct base Variant type.");
		}
		List<MethodInfo> methods;
		dummy.get_method_list(&methods);

		for (const MethodInfo &E : methods) {
			if (E.name == p_function) {
				function_signature_from_info(E, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
				// Cannot use non-const methods on enums.
				if (!r_method_flags.has_flag(METHOD_FLAG_STATIC) && was_enum && !(E.flags & METHOD_FLAG_CONST)) {
					push_error(vformat(R"*(Cannot call non-const Dictionary function "%s()" on enum "%s".)*", p_function, p_base_type.enum_type), p_source);
				}
				return true;
			}
		}

		return false;
	}

	StringName base_native = p_base_type.native_type;
	if (base_native != StringName()) {
		// Empty native class might happen in some Script implementations.
		// Just ignore it.
		if (!class_exists(base_native)) {
			push_error(vformat("Native class %s used in script doesn't exist or isn't exposed.", base_native), p_source);
			return false;
		} else if (p_is_constructor && ClassDB::is_abstract(base_native)) {
			if (p_base_type.kind == GDScriptParser::DataType::CLASS) {
				push_error(vformat(R"(Class "%s" cannot be constructed as it is based on abstract native class "%s".)", p_base_type.class_type->fqcn.get_file(), base_native), p_source);
			} else if (p_base_type.kind == GDScriptParser::DataType::SCRIPT) {
				push_error(vformat(R"(Script "%s" cannot be constructed as it is based on abstract native class "%s".)", p_base_type.script_path.get_file(), base_native), p_source);
			} else {
				push_error(vformat(R"(Native class "%s" cannot be constructed as it is abstract.)", base_native), p_source);
			}
			return false;
		}
	}

	if (p_is_constructor) {
		function_name = GDScriptLanguage::get_singleton()->strings._init;
		r_method_flags.set_flag(METHOD_FLAG_STATIC);
	}

	GDScriptParser::ClassNode *base_class = p_base_type.class_type;
	GDScriptParser::ClassNode *original_base_class = base_class;
	GDScriptParser::FunctionNode *found_function = nullptr;
	// The class that declares `found_function`, used to specialize a generic signature against the
	// (possibly more-derived) receiver type so inherited `T`-typed parameters/returns become concrete.
	GDScriptParser::ClassNode *found_in_class = nullptr;

	while (found_function == nullptr && base_class != nullptr) {
		if (base_class->has_member(function_name)) {
			const GDScriptParser::ClassNode::Member &member = base_class->get_member(function_name);
			if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				const GDScriptParser::DataType member_type = member.get_datatype();
				if (member_type.is_set() && member_type.kind == GDScriptParser::DataType::BUILTIN && member_type.builtin_type == Variant::CALLABLE) {
					return false;
				}
				push_error(vformat(R"(Member "%s" is not a function.)", function_name), p_source);
				return false;
			}

			resolve_class_member(base_class, function_name, p_source);
			found_function = member.function;
			found_in_class = base_class;
		}

		resolve_class_inheritance(base_class, p_source);
		base_class = base_class->base_type.class_type;
	}

	// Resolve calls to methods flattened in from applied traits, both when the base is a
	// trait (trait-requires-trait) and when it is a class that applies traits directly.
	if (found_function == nullptr && original_base_class != nullptr && (original_base_class->is_trait || !original_base_class->used_traits.is_empty())) {
		resolve_trait_uses(original_base_class, p_source);
		for (GDScriptParser::ClassNode *trait : original_base_class->resolved_traits) {
			if (trait == nullptr || !trait->has_member(function_name)) {
				continue;
			}

			const GDScriptParser::ClassNode::Member &member = trait->get_member(function_name);
			if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				const GDScriptParser::DataType member_type = member.get_datatype();
				if (member_type.is_set() && member_type.kind == GDScriptParser::DataType::BUILTIN && member_type.builtin_type == Variant::CALLABLE) {
					return false;
				}
				push_error(vformat(R"(Member "%s" is not a function.)", function_name), p_source);
				return false;
			}

			resolve_class_member(trait, function_name, p_source);
			found_function = member.function;
			found_in_class = trait;
			break;
		}
	}

	if (found_function != nullptr) {
		if (r_found_function) {
			*r_found_function = found_function;
		}
		if (found_function->is_abstract) {
			r_method_flags.set_flag(METHOD_FLAG_VIRTUAL_REQUIRED);
		}
		if (r_is_noreturn) {
			*r_is_noreturn = found_function->is_noreturn;
		}
		if (p_is_constructor || found_function->is_static) {
			r_method_flags.set_flag(METHOD_FLAG_STATIC);
		}
		if (found_function->is_coroutine) {
			r_method_flags.set_flag(METHOD_FLAG_ASYNC);
		}
		// Specialize the signature against the receiver so inherited or directly-applied type
		// arguments substitute `T`-typed parameters and return into concrete types.
		const GDScriptParser::DataType specialized_base = specialize_ancestor_type(p_base_type, found_in_class);
		for (int i = 0; i < found_function->parameters.size(); i++) {
			r_par_types.push_back(substitute_member_type(found_function->parameters[i]->get_datatype(), specialized_base, found_function));
			if (found_function->parameters[i]->initializer != nullptr) {
				r_default_arg_count++;
			}
		}
		if (found_function->is_vararg()) {
			r_method_flags.set_flag(METHOD_FLAG_VARARG);
		}
		r_return_type = p_is_constructor ? p_base_type : substitute_member_type(found_function->get_datatype(), specialized_base, found_function);
		r_return_type.is_meta_type = false;
		r_return_type.is_coroutine = found_function->is_coroutine;

		return true;
	}

	Ref<Script> base_script = p_base_type.script_type;

	while (base_script.is_valid() && base_script->has_method(function_name)) {
		MethodInfo info = base_script->get_method_info(function_name);

		if (!(info == MethodInfo())) {
			return function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		}
		base_script = base_script->get_base_script();
	}

	// If the base is a script, it might be trying to access members of the Script class itself.
	if (p_base_type.is_meta_type && !p_is_constructor && (p_base_type.kind == GDScriptParser::DataType::SCRIPT || p_base_type.kind == GDScriptParser::DataType::CLASS)) {
		MethodInfo info;
		StringName script_class = p_base_type.kind == GDScriptParser::DataType::SCRIPT ? p_base_type.script_type->get_class_name() : StringName(GDScript::get_class_static());

		if (ClassDB::get_method_info(script_class, function_name, &info)) {
			return function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		}
	}

	auto argument_can_be_string_name = [&](const GDScriptParser::CallNode *p_call, int p_argument_index) -> bool {
		if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return false;
		}

		const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
		if (argument == nullptr) {
			return false;
		}

		if (argument->is_constant) {
			const Variant::Type value_type = argument->reduced_value.get_type();
			return value_type == Variant::STRING || value_type == Variant::STRING_NAME;
		}

		const GDScriptParser::DataType argument_type = argument->get_datatype();
		if (argument_type.is_variant() || !argument_type.is_hard_type()) {
			return true;
		}

		const GDScriptParser::DataType string_name_type = type_from_property(PropertyInfo(Variant::STRING_NAME, ""), true);
		const GDScriptParser::DataType string_type = type_from_property(PropertyInfo(Variant::STRING, ""), true);
		return is_type_compatible(string_name_type, argument_type, true) || is_type_compatible(string_type, argument_type, true);
	};
	auto argument_can_be_node_path = [&](const GDScriptParser::CallNode *p_call, int p_argument_index) -> bool {
		if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return false;
		}

		const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
		if (argument == nullptr) {
			return false;
		}

		if (argument->is_constant) {
			return argument->reduced_value.get_type() == Variant::NODE_PATH;
		}

		const GDScriptParser::DataType argument_type = argument->get_datatype();
		if (argument_type.is_variant() || !argument_type.is_hard_type()) {
			return true;
		}

		const GDScriptParser::DataType node_path_type = type_from_property(PropertyInfo(Variant::NODE_PATH, ""), true);
		return is_type_compatible(node_path_type, argument_type, true);
	};
	auto push_strict_dynamic_reflection_error = [&](const GDScriptParser::CallNode *p_call, int p_argument_index, const char *p_kind) {
		if (!strict_dynamic_checks || p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return;
		}
		push_error(vformat(R"*(Cannot use dynamic %s for "%s()" on type "%s" in strict dynamic mode.)*",
						   p_kind,
						   p_call->function_name,
						   p_base_type.to_string()),
				p_call->arguments[p_argument_index]);
	};
	auto push_strict_unresolved_reflection_error = [&](const GDScriptParser::CallNode *p_call, int p_argument_index, const char *p_kind, const String &p_name) {
		if (!strict_dynamic_checks || p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
			return;
		}
		String kind = p_kind;
		if (kind.ends_with(" name")) {
			kind = kind.substr(0, kind.length() - 5);
		}
		push_error(vformat(R"*(Cannot resolve %s "%s" on type "%s" for "%s()" in strict dynamic mode.)*",
						   kind,
						   p_name,
						   p_base_type.to_string(),
						   p_call->function_name),
				p_call->arguments[p_argument_index]);
	};

	if (p_function == SNAME("call") || p_function == SNAME("call_deferred") || p_function == SNAME("callv")) {
		const GDScriptParser::CallNode *call = p_source != nullptr && p_source->type == GDScriptParser::Node::CALL ? static_cast<const GDScriptParser::CallNode *>(p_source) : nullptr;
		StringName method_name;
		const bool has_constant_method_name = call != nullptr && string_name_from_constant_arg(call, 0, method_name);
		if (has_constant_method_name && method_name != SNAME("call") && method_name != SNAME("call_deferred") && method_name != SNAME("callv")) {
			GDScriptParser::DataType callable_type;
			if (callable_type_from_method(p_base_type, method_name, p_source, callable_type)) {
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				if (p_function == SNAME("callv")) {
					r_default_arg_count = 0;
					r_return_type = callable_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : callable_type.method_return_type[0];
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::ARRAY, "arg_array"), true));
					validate_callable_array_literal_args(callable_type.method_parameter_types, callable_type.method_info.default_arguments.size(), callable_type.method_info.flags & METHOD_FLAG_VARARG, array_literal_argument(call, 1), p_function);
				} else {
					r_default_arg_count = callable_type.method_info.default_arguments.size();
					if (callable_type.method_info.flags & METHOD_FLAG_VARARG) {
						r_method_flags.set_flag(METHOD_FLAG_VARARG);
					}
					r_return_type = p_function == SNAME("call_deferred") || callable_type.method_return_type.is_empty() ? type_from_property(PropertyInfo(Variant::NIL, "")) : callable_type.method_return_type[0];
					for (const GDScriptParser::DataType &parameter_type : callable_type.method_parameter_types) {
						r_par_types.push_back(parameter_type);
					}
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "method", method_name);
		} else if (!has_constant_method_name && argument_can_be_string_name(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "method name");
		}
	}

	if ((p_function == SNAME("rpc") || p_function == SNAME("rpc_id")) && is_node_compatible_type(p_base_type)) {
		const GDScriptParser::CallNode *call = p_source != nullptr && p_source->type == GDScriptParser::Node::CALL ? static_cast<const GDScriptParser::CallNode *>(p_source) : nullptr;
		const int method_arg_index = p_function == SNAME("rpc_id") ? 1 : 0;
		StringName method_name;
		const bool has_constant_method_name = call != nullptr && string_name_from_constant_arg(call, method_arg_index, method_name);
		if (has_constant_method_name && method_name != SNAME("rpc") && method_name != SNAME("rpc_id")) {
			GDScriptParser::DataType callable_type;
			if (callable_type_from_method(p_base_type, method_name, p_source, callable_type)) {
				r_default_arg_count = callable_type.method_info.default_arguments.size();
				r_method_flags = METHOD_FLAGS_DEFAULT;
				if (callable_type.method_info.flags & METHOD_FLAG_VARARG) {
					r_method_flags.set_flag(METHOD_FLAG_VARARG);
				}
				r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
				if (p_function == SNAME("rpc_id")) {
					r_par_types.push_back(type_from_property(PropertyInfo(Variant::INT, "peer_id"), true));
				}
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true));
				for (const GDScriptParser::DataType &parameter_type : callable_type.method_parameter_types) {
					r_par_types.push_back(parameter_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, method_arg_index, "method", method_name);
		} else if (!has_constant_method_name && argument_can_be_string_name(call, method_arg_index)) {
			push_strict_dynamic_reflection_error(call, method_arg_index, "method name");
		}
	}

	if (p_function == SNAME("get") || p_function == SNAME("set") || p_function == SNAME("set_deferred")) {
		const GDScriptParser::CallNode *call = p_source != nullptr && p_source->type == GDScriptParser::Node::CALL ? static_cast<const GDScriptParser::CallNode *>(p_source) : nullptr;
		StringName property_name;
		const bool has_constant_property_name = call != nullptr && string_name_from_constant_arg(call, 0, property_name);
		if (has_constant_property_name) {
			GDScriptParser::DataType property_type;
			if (property_type_from_receiver(p_base_type, property_name, p_source, property_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "property"), true));
				if (p_function == SNAME("get")) {
					r_return_type = property_type;
				} else {
					r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
					r_par_types.push_back(property_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "property", property_name);
		} else if (!has_constant_property_name && argument_can_be_string_name(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "property name");
		}
	}

	if (p_function == SNAME("get_indexed") || p_function == SNAME("set_indexed")) {
		const GDScriptParser::CallNode *call = p_source != nullptr && p_source->type == GDScriptParser::Node::CALL ? static_cast<const GDScriptParser::CallNode *>(p_source) : nullptr;
		Vector<StringName> property_path;
		const bool has_constant_property_path = call != nullptr && property_path_from_constant_arg(call, 0, property_path);
		if (has_constant_property_path) {
			GDScriptParser::DataType property_type;
			if (property_type_from_indexed_receiver(p_base_type, property_path, p_source, property_type)) {
				r_default_arg_count = 0;
				r_method_flags = METHOD_FLAGS_DEFAULT;
				r_par_types.push_back(type_from_property(PropertyInfo(Variant::NODE_PATH, "property_path"), true));
				if (p_function == SNAME("get_indexed")) {
					r_return_type = property_type;
				} else {
					r_return_type = type_from_property(PropertyInfo(Variant::NIL, ""));
					r_par_types.push_back(property_type);
				}
				return true;
			}
			push_strict_unresolved_reflection_error(call, 0, "property path", String(NodePath(call->arguments[0]->reduced_value)));
		} else if (!has_constant_property_path && argument_can_be_node_path(call, 0)) {
			push_strict_dynamic_reflection_error(call, 0, "property path");
		}
	}

	if (p_is_constructor) {
		// Native types always have a default constructor.
		r_return_type = p_base_type;
		r_return_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		r_return_type.is_meta_type = false;
		return true;
	}

	MethodInfo info;
	if (ClassDB::get_method_info(base_native, function_name, &info)) {
		bool valid = function_signature_from_info(info, r_return_type, r_par_types, r_default_arg_count, r_method_flags);
		if (valid && Engine::get_singleton()->has_singleton(base_native)) {
			r_method_flags.set_flag(METHOD_FLAG_STATIC);
		}
		if (valid && r_is_noreturn && base_native == SNAME("OS") && function_name == SNAME("crash")) {
			*r_is_noreturn = true;
		}
#ifdef DEBUG_ENABLED
		MethodBind *native_method = ClassDB::get_method(base_native, function_name);
		if (native_method && r_native_class) {
			*r_native_class = native_method->get_instance_class();
		}
#endif // DEBUG_ENABLED
		return valid;
	}

	return false;
}

bool GDScriptAnalyzer::function_signature_from_info(const MethodInfo &p_info, GDScriptParser::DataType &r_return_type, List<GDScriptParser::DataType> &r_par_types, int &r_default_arg_count, BitField<MethodFlags> &r_method_flags) {
	r_return_type = type_from_property(p_info.return_val);
	r_return_type.is_coroutine = (p_info.flags & METHOD_FLAG_ASYNC) != 0;
	r_default_arg_count = p_info.default_arguments.size();
	r_method_flags = p_info.flags;

	for (const PropertyInfo &E : p_info.arguments) {
		r_par_types.push_back(type_from_property(E, true));
	}
	return true;
}

bool GDScriptAnalyzer::callable_signature_from_type(const GDScriptParser::DataType &p_callable_type, Vector<GDScriptParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const {
	if (p_callable_type.kind != GDScriptParser::DataType::BUILTIN || p_callable_type.builtin_type != Variant::CALLABLE || !p_callable_type.has_method_signature) {
		return false;
	}

	r_par_types.clear();
	r_default_arg_count = 0;
	r_is_vararg = false;

	if (p_callable_type.has_explicit_method_signature) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	if (p_callable_type.method_parameter_types.size() == p_callable_type.method_info.arguments.size()) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	for (const PropertyInfo &E : p_callable_type.method_info.arguments) {
		r_par_types.push_back(type_from_property(E, true));
	}
	r_default_arg_count = p_callable_type.method_info.default_arguments.size();
	r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
	return true;
}

GDScriptParser::DataType GDScriptAnalyzer::plain_callable_type() const {
	return type_from_property(PropertyInfo(Variant::CALLABLE, ""));
}

GDScriptParser::DataType GDScriptAnalyzer::explicit_callable_type_from_signature(const GDScriptParser::DataType &p_return_type, const Vector<GDScriptParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const {
	GDScriptParser::DataType callable_type = plain_callable_type();
	callable_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_parameter_types;
	callable_type.method_return_type.push_back(p_return_type);
	callable_type.method_info.return_val = p_return_type.to_property_info("");
	for (int i = 0; i < p_parameter_types.size(); i++) {
		callable_type.method_info.arguments.push_back(p_parameter_types[i].to_property_info("arg" + itos(i + 1)));
	}
	callable_type.method_info.default_arguments.resize(p_default_arg_count);
	if (p_is_vararg) {
		callable_type.method_info.flags |= METHOD_FLAG_VARARG;
	}
	return callable_type;
}

GDScriptParser::DataType GDScriptAnalyzer::transformed_callable_type(const GDScriptParser::DataType &p_source_callable_type, const Vector<GDScriptParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const {
	GDScriptParser::DataType callable_type = p_source_callable_type;
	callable_type.kind = GDScriptParser::DataType::BUILTIN;
	callable_type.builtin_type = Variant::CALLABLE;
	callable_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_parameter_types;
	callable_type.method_extra_allowed_argument_counts.clear();
	callable_type.method_unbound_argument_count = 0;
	callable_type.method_info.arguments.clear();
	for (int i = 0; i < p_parameter_types.size(); i++) {
		callable_type.method_info.arguments.push_back(p_parameter_types[i].to_property_info("arg" + itos(i + 1)));
	}
	callable_type.method_info.default_arguments.clear();
	callable_type.method_info.default_arguments.resize(p_default_arg_count);
	if (p_is_vararg) {
		callable_type.method_info.flags |= METHOD_FLAG_VARARG;
	} else {
		callable_type.method_info.flags &= ~uint32_t(METHOD_FLAG_VARARG);
	}
	if (!callable_type.method_return_type.is_empty()) {
		callable_type.method_info.return_val = callable_type.method_return_type[0].to_property_info("");
	}
	return callable_type;
}

GDScriptParser::DataType GDScriptAnalyzer::explicit_callable_type_from_info(const MethodInfo &p_info) const {
	GDScriptParser::DataType callable_type = make_callable_type(p_info);
	callable_type.method_parameter_types.clear();
	for (const PropertyInfo &argument : p_info.arguments) {
		callable_type.method_parameter_types.push_back(type_from_property(argument, true));
	}
	callable_type.method_return_type.clear();
	callable_type.method_return_type.push_back(type_from_property(p_info.return_val));
	callable_type.has_explicit_method_signature = true;
	return callable_type;
}

bool GDScriptAnalyzer::string_name_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_argument_index, StringName &r_name) const {
	if (p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}

	const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || !argument->is_constant) {
		return false;
	}

	const Variant value = argument->reduced_value;
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	r_name = value;
	return true;
}

bool GDScriptAnalyzer::call_argument_can_be_string_name(const GDScriptParser::CallNode *p_call, int p_argument_index) {
	if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}

	const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr) {
		return false;
	}

	if (argument->is_constant) {
		const Variant::Type value_type = argument->reduced_value.get_type();
		return value_type == Variant::STRING || value_type == Variant::STRING_NAME;
	}

	const GDScriptParser::DataType argument_type = argument->get_datatype();
	if (argument_type.is_variant() || !argument_type.is_hard_type()) {
		return true;
	}

	const GDScriptParser::DataType string_name_type = type_from_property(PropertyInfo(Variant::STRING_NAME, ""), true);
	const GDScriptParser::DataType string_type = type_from_property(PropertyInfo(Variant::STRING, ""), true);
	return is_type_compatible(string_name_type, argument_type, true) || is_type_compatible(string_type, argument_type, true);
}

bool GDScriptAnalyzer::merge_inferred_type_argument(const GDScriptParser::DataType &p_existing, const GDScriptParser::DataType &p_candidate, GDScriptParser::DataType &r_merged) {
	// Type parameters are invariant (epic #125 design): a parameter solved from several arguments
	// must resolve to the same type each time. Differing types conflict and require explicit
	// application.
	const bool existing_is_parameter = p_existing.kind == GDScriptParser::DataType::TYPE_PARAMETER;
	const bool candidate_is_parameter = p_candidate.kind == GDScriptParser::DataType::TYPE_PARAMETER;
	if (existing_is_parameter || candidate_is_parameter) {
		// A type-parameter argument (e.g. an outer `U` forwarded into this call) unifies only with
		// the identical parameter. `is_type_compatible()` treats an erased parameter as Variant-like
		// and would otherwise merge `U` with an unrelated concrete type without flagging a conflict.
		if (existing_is_parameter && candidate_is_parameter &&
				p_existing.type_parameter_name == p_candidate.type_parameter_name &&
				p_existing.type_parameter_scope == p_candidate.type_parameter_scope) {
			r_merged = p_existing;
			return true;
		}
		return false;
	}

	// Concrete types: mutual assignability without implicit conversion is the invariant equality
	// test, tolerating incidental DataType field differences between two arguments of the same type.
	if (is_type_compatible(p_existing, p_candidate) && is_type_compatible(p_candidate, p_existing)) {
		r_merged = p_existing;
		return true;
	}
	return false;
}

void GDScriptAnalyzer::collect_type_parameter_bindings(const GDScriptParser::DataType &p_parameter_type, const GDScriptParser::DataType &p_argument_type,
		HashMap<StringName, GDScriptParser::DataType> &r_bindings, HashSet<StringName> &r_conflicts) {
	if (p_parameter_type.kind == GDScriptParser::DataType::TYPE_PARAMETER &&
			p_parameter_type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_METHOD) {
		// Only a usable, concrete argument type constrains a parameter; a Variant or untyped
		// argument leaves it open for another argument (or explicit application) to solve.
		if (!p_argument_type.is_set() || p_argument_type.is_variant() || !p_argument_type.is_hard_type()) {
			return;
		}

		GDScriptParser::DataType candidate = p_argument_type;
		candidate.is_meta_type = false;
		candidate.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

		const StringName &name = p_parameter_type.type_parameter_name;
		if (r_conflicts.has(name)) {
			return;
		}
		GDScriptParser::DataType *existing = r_bindings.getptr(name);
		if (existing == nullptr) {
			r_bindings.insert(name, candidate);
			return;
		}
		GDScriptParser::DataType merged;
		if (merge_inferred_type_argument(*existing, candidate, merged)) {
			*existing = merged;
		} else {
			r_conflicts.insert(name);
		}
		return;
	}

	// Unify structurally through matching containers (`Array[T]`, `Dictionary[K, V]`) and
	// specialized handles (`Box[T]`), so a parameter nested inside a type argument is solved too.
	const int parameter_element_count = p_parameter_type.container_element_types.size();
	if (parameter_element_count > 0 && parameter_element_count == p_argument_type.container_element_types.size()) {
		for (int i = 0; i < parameter_element_count; i++) {
			collect_type_parameter_bindings(p_parameter_type.container_element_types[i], p_argument_type.container_element_types[i], r_bindings, r_conflicts);
		}
	}

	const int parameter_argument_count = p_parameter_type.type_arguments.size();
	if (parameter_argument_count > 0 && parameter_argument_count == p_argument_type.type_arguments.size()) {
		for (int i = 0; i < parameter_argument_count; i++) {
			collect_type_parameter_bindings(p_parameter_type.type_arguments[i], p_argument_type.type_arguments[i], r_bindings, r_conflicts);
		}
	}
}

bool GDScriptAnalyzer::resolve_explicit_type_argument(GDScriptParser::ExpressionNode *p_expression, GDScriptParser::DataType &r_type_argument) {
	if (p_expression == nullptr) {
		return false;
	}

	if (p_expression->type == GDScriptParser::Node::IDENTIFIER) {
		GDScriptParser::IdentifierNode *identifier = static_cast<GDScriptParser::IdentifierNode *>(p_expression);
		const Variant::Type builtin_type = GDScriptParser::get_builtin_type(identifier->name);
		if (builtin_type < Variant::VARIANT_MAX) {
			r_type_argument = type_from_metatype(make_builtin_meta_type(builtin_type));
			return true;
		}

		GDScriptParser::DataType type_parameter;
		if (resolve_type_parameter(identifier->name, type_parameter)) {
			r_type_argument = type_parameter;
			return true;
		}

		reduce_identifier(identifier, true);
		GDScriptParser::DataType identifier_type = identifier->get_datatype();
		if (identifier_type.is_set() && identifier_type.is_meta_type) {
			r_type_argument = type_from_metatype(identifier_type);
			return true;
		}
		return false;
	}

	if (p_expression->type == GDScriptParser::Node::SUBSCRIPT) {
		// A nested type argument parsed as a subscript, such as `Array[Element]` (single element) or
		// `Dictionary[Key, Value]` (a two-argument container). A generic class handle (`Box[int]`)
		// as a nested argument is not representable here yet, so reject it rather than silently
		// building a wrong type.
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute || subscript->base == nullptr || subscript->index == nullptr) {
			return false;
		}
		GDScriptParser::DataType base_argument;
		if (!resolve_explicit_type_argument(subscript->base, base_argument)) {
			return false;
		}
		if (base_argument.kind != GDScriptParser::DataType::BUILTIN) {
			return false;
		}

		Vector<GDScriptParser::ExpressionNode *> element_expressions;
		if (subscript->type_arguments.is_empty()) {
			element_expressions.push_back(subscript->index);
		} else {
			element_expressions = subscript->type_arguments;
		}

		if (base_argument.builtin_type == Variant::ARRAY) {
			if (element_expressions.size() != 1) {
				return false;
			}
		} else if (base_argument.builtin_type == Variant::DICTIONARY) {
			if (element_expressions.size() != 2) {
				return false;
			}
		} else {
			return false;
		}

		for (int i = 0; i < element_expressions.size(); i++) {
			GDScriptParser::DataType element_argument;
			if (!resolve_explicit_type_argument(element_expressions[i], element_argument)) {
				return false;
			}
			base_argument.set_container_element_type(i, element_argument);
		}
		r_type_argument = base_argument;
		return true;
	}

	return false;
}

static void collect_method_type_parameter_bounds(const GDScriptParser::DataType &p_type, HashMap<StringName, GDScriptParser::DataType> &r_bounds) {
	if (p_type.kind == GDScriptParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_METHOD &&
			!p_type.type_parameter_bound.is_empty() && !r_bounds.has(p_type.type_parameter_name)) {
		r_bounds.insert(p_type.type_parameter_name, p_type.type_parameter_bound[0]);
	}
	for (const GDScriptParser::DataType &element : p_type.container_element_types) {
		collect_method_type_parameter_bounds(element, r_bounds);
	}
	for (const GDScriptParser::DataType &argument : p_type.type_arguments) {
		collect_method_type_parameter_bounds(argument, r_bounds);
	}
}

void GDScriptAnalyzer::apply_generic_method_call(GDScriptParser::CallNode *p_call, GDScriptParser::FunctionNode *p_function,
		List<GDScriptParser::DataType> &r_par_types, GDScriptParser::DataType &r_return_type) {
	const Vector<GDScriptParser::TypeParameterNode *> &type_parameters = p_function->type_parameters;
	if (type_parameters.is_empty()) {
		return;
	}

	// A parameter that cannot be solved falls back to Variant so the rest of the call stays
	// type-checkable after the inference error is reported, without cascading "cannot infer" noise.
	GDScriptParser::DataType unresolved_fallback;
	unresolved_fallback.kind = GDScriptParser::DataType::VARIANT;
	unresolved_fallback.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

	HashMap<StringName, GDScriptParser::DataType> bindings;
	// Parameters that could not be solved (conflicting or unconstrained); their fallback Variant
	// binding must not be re-reported as a bound violation.
	HashSet<StringName> failed_parameters;

	// Explicit type arguments (`swap[int](...)`) short-circuit inference.
	bool explicit_application = false;
	if (p_call->callee != nullptr && p_call->callee->type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_call->callee);
		if (!subscript->is_attribute && subscript->index != nullptr) {
			explicit_application = true;

			Vector<GDScriptParser::ExpressionNode *> argument_expressions;
			if (subscript->type_arguments.is_empty()) {
				argument_expressions.push_back(subscript->index);
			} else {
				argument_expressions = subscript->type_arguments;
			}

			// Resolve positionally: a failed argument keeps its slot (filled with the Variant
			// fallback and flagged) so a later argument is never shifted into an earlier type
			// parameter, and the arity check sees the count the user actually wrote.
			Vector<GDScriptParser::DataType> explicit_arguments;
			Vector<bool> explicit_argument_failed;
			for (GDScriptParser::ExpressionNode *argument_expression : argument_expressions) {
				GDScriptParser::DataType type_argument;
				if (resolve_explicit_type_argument(argument_expression, type_argument)) {
					explicit_arguments.push_back(type_argument);
					explicit_argument_failed.push_back(false);
				} else {
					push_error(vformat(R"*(Could not resolve the explicit type argument for generic method "%s()".)*", p_function->identifier->name), argument_expression);
					explicit_arguments.push_back(unresolved_fallback);
					explicit_argument_failed.push_back(true);
				}
			}

			if (explicit_arguments.size() != type_parameters.size()) {
				push_error(vformat(R"*(Generic method "%s()" expects %d type argument(s), but %d %s given.)*", p_function->identifier->name, type_parameters.size(), explicit_arguments.size(), explicit_arguments.size() == 1 ? "was" : "were"), subscript);
				explicit_arguments.clear();
				explicit_argument_failed.clear();
			}

			const int binding_count = MIN(explicit_arguments.size(), type_parameters.size());
			for (int i = 0; i < binding_count; i++) {
				const GDScriptParser::TypeParameterNode *parameter = type_parameters[i];
				if (parameter == nullptr || parameter->identifier == nullptr) {
					continue;
				}
				bindings.insert(parameter->identifier->name, explicit_arguments[i]);
				if (explicit_argument_failed[i]) {
					// Resolution already failed and was reported; keep the slot bound to Variant and
					// skip its bound check rather than re-diagnosing.
					failed_parameters.insert(parameter->identifier->name);
				}
			}
		}
	}

	// Solve any still-open parameters by unifying each argument against its declared type.
	if (!explicit_application) {
		HashSet<StringName> conflicts;
		int parameter_index = 0;
		for (const GDScriptParser::DataType &declared_parameter_type : r_par_types) {
			if (parameter_index >= p_call->arguments.size()) {
				break;
			}
			const GDScriptParser::ExpressionNode *argument = p_call->arguments[parameter_index];
			if (argument != nullptr) {
				collect_type_parameter_bindings(declared_parameter_type, argument->get_datatype(), bindings, conflicts);
			}
			parameter_index++;
		}

		for (const StringName &conflicted : conflicts) {
			push_error(vformat(R"*(Could not infer type parameter "%s" of generic method "%s()" because its arguments have conflicting types. Apply the type arguments explicitly, e.g. "%s[...](...)".)*", conflicted, p_function->identifier->name, p_function->identifier->name), p_call);
			// Keep the call type-checkable: an unresolved parameter falls back to Variant.
			bindings.insert(conflicted, unresolved_fallback);
			failed_parameters.insert(conflicted);
		}
	}

	// Every parameter must end up bound. Under inference, a still-open parameter is one no
	// argument constrained, which is an error directing the user to explicit application. Under
	// explicit application a gap means resolution already failed and was reported above, so it
	// just falls back without a second diagnostic.
	for (const GDScriptParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		if (!bindings.has(parameter->identifier->name)) {
			if (!explicit_application) {
				push_error(vformat(R"*(Could not infer type parameter "%s" of generic method "%s()" from its arguments. Apply the type arguments explicitly, e.g. "%s[...](...)".)*", parameter->identifier->name, p_function->identifier->name, p_function->identifier->name), p_call);
			}
			bindings.insert(parameter->identifier->name, unresolved_fallback);
			failed_parameters.insert(parameter->identifier->name);
		}
	}

	if (bindings.is_empty()) {
		return;
	}

	// A solved type argument (inferred or explicit) must satisfy its parameter's upper bound. A
	// bound's resolved datatype is cached on its declaration once the parameter is used anywhere
	// (signature or body); the signature traversal is a fallback for bounds reached only there.
	HashMap<StringName, GDScriptParser::DataType> parameter_bounds;
	for (const GDScriptParser::DataType &parameter_type : r_par_types) {
		collect_method_type_parameter_bounds(parameter_type, parameter_bounds);
	}
	collect_method_type_parameter_bounds(r_return_type, parameter_bounds);
	for (const GDScriptParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr || parameter->bound == nullptr) {
			continue;
		}
		// The bound collected from the (already receiver-specialized) signature wins; the raw
		// declaration is only a fallback for a parameter whose bound is reached nowhere in the
		// signature, so it must not overwrite the specialized one (e.g. `[U: T]` with `T := PackedScene`).
		if (parameter_bounds.has(parameter->identifier->name)) {
			continue;
		}
		const GDScriptParser::DataType bound_type = parameter->bound->get_datatype();
		if (bound_type.is_set() && bound_type.kind != GDScriptParser::DataType::UNRESOLVED && bound_type.kind != GDScriptParser::DataType::RESOLVING) {
			parameter_bounds[parameter->identifier->name] = type_from_metatype(bound_type);
		}
	}

	for (const GDScriptParser::TypeParameterNode *parameter : type_parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		const StringName &name = parameter->identifier->name;
		if (failed_parameters.has(name)) {
			continue;
		}
		const GDScriptParser::DataType *binding = bindings.getptr(name);
		const GDScriptParser::DataType *bound = parameter_bounds.getptr(name);
		if (binding == nullptr || bound == nullptr || bound->kind == GDScriptParser::DataType::UNRESOLVED) {
			continue;
		}
		// A dependent bound (`[U: Resource, T: U]`) is resolved against the sibling's solved type,
		// so substitute the collected bindings into the bound before checking.
		const GDScriptParser::DataType effective_bound = GDScriptParser::DataType::substitute(*bound, bindings);
		if (!type_argument_satisfies_bound(*binding, effective_bound)) {
			push_error(vformat(R"*(Type argument "%s" does not satisfy the bound "%s" of type parameter "%s" of generic method "%s()".)*", binding->to_string(), effective_bound.to_string(), name, p_function->identifier->name), p_call);
		}
	}

	for (GDScriptParser::DataType &parameter_type : r_par_types) {
		parameter_type = GDScriptParser::DataType::substitute(parameter_type, bindings);
	}

	// A typed-container return whose element involves a method type parameter (`-> Array[T]`) is erased
	// at runtime: the method, compiled once, returns an untyped container. We still substitute the
	// static type to the concrete container below, so flag the call here (before erasing the marker) so
	// an assignment to a concrete typed container retypes the untyped runtime value.
	if (p_call != nullptr) {
		for (int i = 0; i < r_return_type.container_element_types.size(); i++) {
			if (_signature_type_involves_type_parameter(r_return_type.container_element_types[i])) {
				p_call->returns_erased_container = true;
				break;
			}
		}
	}

	r_return_type = GDScriptParser::DataType::substitute(r_return_type, bindings);
}

void GDScriptAnalyzer::reduce_call_create_proxy(GDScriptParser::CallNode *p_call, GDScriptParser::SubscriptNode *p_callee) {
	// The built-in `create_proxy[T](handler) -> T`: a typed layer over the
	// `create_proxy_dynamic` runtime. The analyzer types the result as T and flags
	// the node; the compiler lowers it to `create_proxy_dynamic(T, handler)` by
	// materializing T's script from the `[T]` type argument.
	GDScriptParser::DataType error_type;
	error_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	error_type.kind = GDScriptParser::DataType::VARIANT;

	// Read the full use-site type-argument list positionally. A single argument is
	// captured in `index`; a multi-argument list (`create_proxy[A, B]`) populates
	// `type_arguments`, with `index` aliasing the first element. `create_proxy[T]`
	// takes exactly one type parameter, so anything other than a single argument is an
	// arity error rather than silently consuming only the first entry.
	Vector<GDScriptParser::ExpressionNode *> argument_expressions;
	if (p_callee->type_arguments.is_empty()) {
		if (p_callee->index != nullptr) {
			argument_expressions.push_back(p_callee->index);
		}
	} else {
		argument_expressions = p_callee->type_arguments;
	}

	if (argument_expressions.size() != 1) {
		push_error(vformat(R"*(create_proxy[T]() expects a single type argument, but %d %s given.)*", argument_expressions.size(), argument_expressions.size() == 1 ? "was" : "were"), p_callee->index != nullptr ? static_cast<GDScriptParser::Node *>(p_callee->index) : static_cast<GDScriptParser::Node *>(p_call));
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Resolve the `[T]` type argument. This also reduces a class-name index as a value,
	// which the compiler later compiles into T's script reference.
	GDScriptParser::DataType type_argument;
	if (!resolve_explicit_type_argument(argument_expressions[0], type_argument)) {
		push_error(R"*(Could not resolve the type argument for "create_proxy[T]()".)*", argument_expressions[0]);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// A forwarded class type parameter is reified onto the instance at construction
	// (e.g. `Mock[Greeter].new()` binds T = Greeter), so the compiler can materialize its
	// bound script at runtime and the runtime guard validates the actual binding. A method
	// type parameter is not reified onto the instance, so it still has no concrete script
	// to recover; reject only that case, pointing at the dynamic fallback.
	if (type_argument.kind == GDScriptParser::DataType::TYPE_PARAMETER &&
			type_argument.type_parameter_scope != GDScriptParser::DataType::TYPE_PARAMETER_CLASS) {
		push_error(vformat(R"*(create_proxy[T]() cannot forward the method type parameter "%s" because method type arguments are not reified at runtime. Use create_proxy_dynamic() with an explicit type instead.)*", type_argument.to_string()), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// A forwarded class type parameter resolves to its bound script only at runtime, so the
	// remaining static checks (trait/abstract and RefCounted-base guards) cannot run here;
	// they are enforced by the runtime guard in `GDScriptProxy::create_proxy` against the
	// actual binding. Type the result as T and let the compiler emit the reified lookup.
	if (type_argument.kind == GDScriptParser::DataType::TYPE_PARAMETER) {
		// Class type parameters are reified per instance, so they are only recoverable from a
		// non-static function. A static function has no instance to read the binding from.
		if (static_context) {
			push_error(vformat(R"*(create_proxy[T]() cannot forward the class type parameter "%s" from a static function because it is reified per instance. Use create_proxy_dynamic() with an explicit type instead.)*", type_argument.to_string()), p_call);
			p_call->set_datatype(error_type);
			mark_node_unsafe(p_call);
			return;
		}

		// The compiler materializes T from the instance's reified bindings, so this call
		// needs `self`. Inside a lambda that does not otherwise touch `self`, mark it as
		// using self so it is invoked with the instance rather than a null one.
		mark_lambda_use_self();

		type_argument.is_meta_type = false;
		type_argument.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		p_call->is_proxy_construct = true;
		p_call->set_datatype(type_argument);

		if (p_call->arguments.size() != 1) {
			push_error(vformat(R"*(create_proxy[T]() expects a single handler argument, but %d %s given.)*", p_call->arguments.size(), p_call->arguments.size() == 1 ? "was" : "were"), p_call);
		} else {
			const GDScriptParser::DataType handler_type = p_call->arguments[0]->get_datatype();
			if (handler_type.is_hard_type() && !handler_type.is_variant() && !(handler_type.kind == GDScriptParser::DataType::BUILTIN && handler_type.builtin_type == Variant::CALLABLE)) {
				push_error(vformat(R"*(create_proxy[T]() expects a Callable handler, but the argument is of type "%s".)*", handler_type.to_string()), p_call->arguments[0]);
			}
		}
		return;
	}

	// The target must be a trait or abstract type (mirrors the runtime guard in
	// GDScriptProxy::create_proxy): only those define a contract to intercept. When the
	// type cannot be classified statically (e.g. an external script not yet compiled), the
	// runtime guard still applies, so this only hard-rejects the cases known to be invalid.
	bool target_invalid = false;
	switch (type_argument.kind) {
		case GDScriptParser::DataType::CLASS:
			if (type_argument.class_type != nullptr) {
				target_invalid = !(type_argument.class_type->is_trait || type_argument.class_type->is_abstract);
			}
			break;
		case GDScriptParser::DataType::SCRIPT: {
			Ref<GDScript> gdscript = type_argument.script_type;
			if (gdscript.is_valid() && gdscript->is_valid()) {
				target_invalid = !(gdscript->is_trait_type() || gdscript->is_abstract());
			}
		} break;
		case GDScriptParser::DataType::BUILTIN:
		case GDScriptParser::DataType::NATIVE:
		case GDScriptParser::DataType::ENUM:
			target_invalid = true;
			break;
		default:
			break;
	}
	if (target_invalid) {
		push_error(vformat(R"*(create_proxy[T]() requires a trait or abstract type as its type argument, but "%s" is neither.)*", type_argument.to_string()), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Mirror the runtime guard in GDScriptProxy::_validate_proxy_target: the proxy host is a
	// RefCounted, so a contract rooted on any other native base (Node, Resource, ...) cannot be
	// represented soundly. Resolve the native base the same way is_node_compatible_type does; if
	// it cannot be determined statically, defer to the runtime guard rather than false-reject.
	StringName native_base = type_argument.native_type;
	if (native_base == StringName() && type_argument.kind == GDScriptParser::DataType::CLASS) {
		const GDScriptParser::ClassNode *class_node = type_argument.class_type;
		while (class_node != nullptr) {
			if (class_node->base_type.kind == GDScriptParser::DataType::CLASS) {
				class_node = class_node->base_type.class_type;
				continue;
			}
			if (class_node->base_type.native_type != StringName()) {
				native_base = class_node->base_type.native_type;
			} else if (class_node->base_type.script_type.is_valid()) {
				native_base = class_node->base_type.script_type->get_instance_base_type();
			}
			break;
		}
	}
	// The runtime guard requires the base to be exactly RefCounted (not merely a descendant such
	// as Resource), since proxying other native bases is not supported yet. Match that exactly.
	if (native_base != StringName() && native_base != SNAME("RefCounted") && class_exists(native_base)) {
		push_error(vformat(R"*(create_proxy[T]() requires a trait or abstract type rooted on RefCounted, but "%s" has native base "%s".)*", type_argument.to_string(), native_base), p_call);
		p_call->set_datatype(error_type);
		mark_node_unsafe(p_call);
		return;
	}

	// Exactly one handler argument, which must be callable.
	if (p_call->arguments.size() != 1) {
		push_error(vformat(R"*(create_proxy[T]() expects a single handler argument, but %d %s given.)*", p_call->arguments.size(), p_call->arguments.size() == 1 ? "was" : "were"), p_call);
	} else {
		const GDScriptParser::DataType handler_type = p_call->arguments[0]->get_datatype();
		if (handler_type.is_hard_type() && !handler_type.is_variant() && !(handler_type.kind == GDScriptParser::DataType::BUILTIN && handler_type.builtin_type == Variant::CALLABLE)) {
			push_error(vformat(R"*(create_proxy[T]() expects a Callable handler, but the argument is of type "%s".)*", handler_type.to_string()), p_call->arguments[0]);
		}
	}

	// The result is an instance of T.
	type_argument.is_meta_type = false;
	type_argument.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	p_call->is_proxy_construct = true;
	p_call->set_datatype(type_argument);
}

bool GDScriptAnalyzer::callable_type_from_method(const GDScriptParser::DataType &p_receiver_type, const StringName &p_method_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_callable_type) {
	GDScriptParser::DataType return_type;
	List<GDScriptParser::DataType> parameter_types;
	int default_arg_count = 0;
	BitField<MethodFlags> method_flags = {};
	if (!get_function_signature(p_source, false, p_receiver_type, p_method_name, return_type, parameter_types, default_arg_count, method_flags)) {
		return false;
	}

	Vector<GDScriptParser::DataType> parameter_type_vector;
	for (const GDScriptParser::DataType &parameter_type : parameter_types) {
		parameter_type_vector.push_back(parameter_type);
	}
	r_callable_type = explicit_callable_type_from_signature(return_type, parameter_type_vector, default_arg_count, method_flags.has_flag(METHOD_FLAG_VARARG));
	return true;
}

bool GDScriptAnalyzer::callable_type_from_constant_method_args(const GDScriptParser::CallNode *p_call, int p_receiver_arg_index, int p_method_arg_index, GDScriptParser::DataType &r_callable_type) {
	if (p_receiver_arg_index < 0 || p_receiver_arg_index >= p_call->arguments.size()) {
		return false;
	}

	StringName method_name;
	if (!string_name_from_constant_arg(p_call, p_method_arg_index, method_name)) {
		return false;
	}

	return callable_type_from_method(p_call->arguments[p_receiver_arg_index]->get_datatype(), method_name, const_cast<GDScriptParser::CallNode *>(p_call), r_callable_type);
}

void GDScriptAnalyzer::validate_strict_callable_method_fallback(const GDScriptParser::CallNode *p_call, const GDScriptParser::DataType &p_receiver_type, int p_method_arg_index) {
	if (!strict_dynamic_checks || p_call == nullptr || p_method_arg_index < 0 || p_method_arg_index >= p_call->arguments.size()) {
		return;
	}

	StringName method_name;
	if (string_name_from_constant_arg(p_call, p_method_arg_index, method_name)) {
		push_error(vformat(R"*(Cannot resolve method "%s" on type "%s" for Callable construction in strict dynamic mode.)*", method_name, p_receiver_type.to_string()), p_call->arguments[p_method_arg_index]);
	} else if (call_argument_can_be_string_name(p_call, p_method_arg_index)) {
		push_error("Cannot use dynamic method name for Callable construction in strict dynamic mode.", p_call->arguments[p_method_arg_index]);
	}
}

bool GDScriptAnalyzer::is_node_compatible_type(const GDScriptParser::DataType &p_type) const {
	StringName native_type = p_type.native_type;

	if (native_type == StringName() && p_type.kind == GDScriptParser::DataType::CLASS) {
		const GDScriptParser::ClassNode *class_node = p_type.class_type;
		while (class_node != nullptr) {
			if (class_node->base_type.kind == GDScriptParser::DataType::CLASS) {
				class_node = class_node->base_type.class_type;
				continue;
			}
			if (class_node->base_type.native_type != StringName()) {
				native_type = class_node->base_type.native_type;
			} else if (class_node->base_type.script_type.is_valid()) {
				native_type = class_node->base_type.script_type->get_instance_base_type();
			}
			break;
		}
	}

	return native_type != StringName() && class_exists(native_type) && ClassDB::is_parent_class(native_type, SNAME("Node"));
}

bool GDScriptAnalyzer::property_type_from_class(GDScriptParser::ClassNode *p_class, const StringName &p_property_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type) {
	for (GDScriptParser::ClassNode *class_node = p_class; class_node != nullptr;) {
		if (class_node->has_member(p_property_name)) {
			if (class_node->get_member(p_property_name).type != GDScriptParser::ClassNode::Member::VARIABLE) {
				return false;
			}

			resolve_class_member(class_node, p_property_name, p_source);
			r_property_type = class_node->get_member(p_property_name).get_datatype();
			return true;
		}

		resolve_class_inheritance(class_node, p_source);
		if (class_node->base_type.kind == GDScriptParser::DataType::CLASS) {
			class_node = class_node->base_type.class_type;
		} else if (class_node->base_type.kind == GDScriptParser::DataType::SCRIPT) {
			return property_type_from_script(class_node->base_type.script_type, p_property_name, r_property_type);
		} else if (class_node->base_type.kind == GDScriptParser::DataType::NATIVE) {
			return property_type_from_native(class_node->base_type.native_type, p_property_name, r_property_type);
		} else {
			class_node = nullptr;
		}
	}

	return false;
}

bool GDScriptAnalyzer::property_type_from_script(const Ref<Script> &p_script, const StringName &p_property_name, GDScriptParser::DataType &r_property_type) const {
	Ref<Script> script = p_script;
	while (script.is_valid()) {
		List<PropertyInfo> property_list;
		script->get_script_property_list(&property_list);

		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name == p_property_name) {
				r_property_type = type_from_property(property_info);
				return true;
			}
		}

		script = script->get_base_script();
	}

	if (p_script.is_valid()) {
		return property_type_from_native(p_script->get_instance_base_type(), p_property_name, r_property_type);
	}

	return false;
}

bool GDScriptAnalyzer::property_type_from_native(const StringName &p_native_type, const StringName &p_property_name, GDScriptParser::DataType &r_property_type) const {
	if (p_native_type == StringName() || !class_exists(p_native_type) || !ClassDB::has_property(p_native_type, p_property_name)) {
		return false;
	}

	const StringName getter_name = ClassDB::get_property_getter(p_native_type, p_property_name);
	MethodBind *getter = getter_name == StringName() ? nullptr : ClassDB::get_method(p_native_type, getter_name);
	if (getter != nullptr) {
		const bool is_read_only = ClassDB::get_property_setter(p_native_type, p_property_name) == StringName();
		r_property_type = type_from_property(getter->get_return_info(), false, is_read_only);
		return true;
	}

	bool is_valid = false;
	const Variant::Type property_type = ClassDB::get_property_type(p_native_type, p_property_name, &is_valid);
	if (!is_valid) {
		return false;
	}

	r_property_type = type_from_property(PropertyInfo(property_type, String(p_property_name)));
	return true;
}

bool GDScriptAnalyzer::property_type_from_receiver(const GDScriptParser::DataType &p_receiver_type, const StringName &p_property_name, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type) {
	switch (p_receiver_type.kind) {
		case GDScriptParser::DataType::CLASS:
			return property_type_from_class(p_receiver_type.class_type, p_property_name, p_source, r_property_type);
		case GDScriptParser::DataType::SCRIPT:
			return property_type_from_script(p_receiver_type.script_type, p_property_name, r_property_type);
		case GDScriptParser::DataType::NATIVE:
			return property_type_from_native(p_receiver_type.native_type, p_property_name, r_property_type);
		default:
			return false;
	}
}

bool GDScriptAnalyzer::property_path_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_argument_index, Vector<StringName> &r_property_path) const {
	if (p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}

	const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || !argument->is_constant || argument->reduced_value.get_type() != Variant::NODE_PATH) {
		return false;
	}

	const NodePath property_path = NodePath(argument->reduced_value).get_as_property_path();
	r_property_path = property_path.get_subnames();
	return !r_property_path.is_empty();
}

bool GDScriptAnalyzer::property_type_from_builtin_member(const GDScriptParser::DataType &p_base_type, const StringName &p_member_name, GDScriptParser::DataType &r_member_type) const {
	if (p_base_type.kind != GDScriptParser::DataType::BUILTIN) {
		return false;
	}

	const Variant::Type builtin_type = p_base_type.builtin_type;
	if (builtin_type == Variant::OBJECT || builtin_type == Variant::ARRAY || builtin_type == Variant::DICTIONARY || builtin_type == Variant::NIL) {
		return false;
	}
	if (Variant::get_member_validated_getter(builtin_type, p_member_name) == nullptr) {
		return false;
	}

	const Variant::Type member_type = Variant::get_member_type(builtin_type, p_member_name);
	if (member_type == Variant::VARIANT_MAX) {
		return false;
	}

	r_member_type = type_from_property(PropertyInfo(member_type, String(p_member_name)));
	return true;
}

bool GDScriptAnalyzer::property_type_from_indexed_receiver(const GDScriptParser::DataType &p_receiver_type, const Vector<StringName> &p_property_path, GDScriptParser::Node *p_source, GDScriptParser::DataType &r_property_type) {
	if (p_property_path.is_empty()) {
		return false;
	}

	if (!property_type_from_receiver(p_receiver_type, p_property_path[0], p_source, r_property_type)) {
		return false;
	}

	for (int i = 1; i < p_property_path.size(); i++) {
		if (!property_type_from_builtin_member(r_property_type, p_property_path[i], r_property_type)) {
			return false;
		}
	}

	return true;
}

GDScriptParser::ArrayNode *GDScriptAnalyzer::array_literal_argument(const GDScriptParser::CallNode *p_call, int p_argument_index) const {
	if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return nullptr;
	}

	GDScriptParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || argument->type != GDScriptParser::Node::ARRAY) {
		return nullptr;
	}

	return static_cast<GDScriptParser::ArrayNode *>(argument);
}

bool GDScriptAnalyzer::call_has_named_arguments(const GDScriptParser::CallNode *p_call) {
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			return true;
		}
	}
	return false;
}

void GDScriptAnalyzer::reject_named_call_arguments(const GDScriptParser::CallNode *p_call) {
	// Named arguments are resolved entirely at compile time against a statically known
	// GDScript signature. For any other callee (builtin constructors, engine utility
	// functions, native methods, or dynamic/`Callable` targets) the parameter names are
	// unavailable, so reject them instead of silently dropping the names.
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			push_error("Named arguments require a statically known GDScript function.", p_call->arguments[i]);
			return;
		}
	}
}

bool GDScriptAnalyzer::canonicalize_named_call_arguments(GDScriptParser::CallNode *p_call, const GDScriptParser::FunctionNode *p_function) {
	// The parser keeps `argument_names` parallel to `arguments`, with an empty name for each
	// positional argument. Map every `name = value` argument to its parameter position, rewrite
	// the call into canonical positional order, and clear the names so the rest of the analyzer,
	// codegen, and the VM see an ordinary positional call.
	if (p_call->argument_names.size() != p_call->arguments.size() || !call_has_named_arguments(p_call)) {
		// Nothing to canonicalize; drop any (all-empty) name metadata for a clean positional call.
		p_call->argument_names.clear();
		return true;
	}

	const int parameter_count = p_function->parameters.size();
	const StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();

	// Rule: once a named argument appears, every following argument must be named.
	int positional_count = 0;
	bool seen_named = false;
	for (int i = 0; i < p_call->arguments.size(); i++) {
		const bool is_named = p_call->argument_names[i] != StringName();
		if (is_named) {
			seen_named = true;
		} else if (seen_named) {
			push_error("Positional argument cannot follow a named argument.", p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		} else {
			positional_count++;
		}
	}

	Vector<GDScriptParser::ExpressionNode *> slots;
	slots.resize(parameter_count);
	for (int i = 0; i < parameter_count; i++) {
		slots.write[i] = nullptr;
	}

	// Positional arguments fill the leading parameter slots in order. Arguments beyond the fixed
	// parameter count belong to a rest parameter and keep their order after the fixed slots.
	Vector<GDScriptParser::ExpressionNode *> rest_arguments;
	for (int i = 0; i < positional_count; i++) {
		if (i < parameter_count) {
			slots.write[i] = p_call->arguments[i];
		} else {
			rest_arguments.push_back(p_call->arguments[i]);
		}
	}

	for (int i = positional_count; i < p_call->arguments.size(); i++) {
		const StringName &argument_name = p_call->argument_names[i];
		if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr && p_function->rest_parameter->identifier->name == argument_name) {
			push_error(vformat(R"(The rest parameter "%s" cannot be passed by name.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		const int *parameter_index = p_function->parameters_indices.getptr(argument_name);
		if (parameter_index == nullptr) {
			push_error(vformat(R"*(Function "%s()" has no parameter named "%s".)*", function_name, argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		if (slots[*parameter_index] != nullptr) {
			push_error(vformat(R"(Parameter "%s" was specified more than once.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		slots.write[*parameter_index] = p_call->arguments[i];
	}

	int max_filled_index = -1;
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			max_filled_index = i;
		}
	}

	// Every parameter slot a named call leaves empty must be resolved here. A required parameter with
	// no value is a compile error named after the parameter. An omitted parameter before the last
	// filled slot has no positional argument, so its constant default must be inlined at the call site
	// to keep the canonical order correct; GDScript defaults run in the callee's scope and may
	// reference `self`, members, or earlier parameters, so only a compile-time-constant default is
	// safe to materialize here, and anything else is a compile error. A trailing omitted parameter is
	// left out so the callee applies its own default at runtime, exactly as a positional call that
	// omits trailing arguments would.
	//
	// The inlined value is the statically resolved callee's default, matching the rest of the
	// feature's compile-time model (the call is also type-checked against that static signature). If
	// a subclass overrides the method with a different default, a base-typed receiver dispatched to
	// that override still receives the static default rather than the override's; aligning this with
	// the callee's runtime default mechanism is a separate design decision tracked as a follow-up.
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			continue;
		}

		const GDScriptParser::ParameterNode *parameter = p_function->parameters[i];
		const StringName parameter_name = parameter->identifier != nullptr ? parameter->identifier->name : StringName();

		if (parameter->initializer == nullptr) {
			// A required parameter received no argument. Mirror the positional too-few-arguments error
			// but name the specific parameter the named call left unfilled.
			push_error(vformat(R"(Missing value for required parameter "%s".)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		if (i >= max_filled_index) {
			// Trailing optional parameter: leave it out so the callee supplies its own default.
			continue;
		}

		// Interior gap with a default. The default must be materialized as a constant at the call
		// site, but two cases are excluded because a baked constant would diverge from how the callee's
		// own default mechanism resolves the value:
		//   - A parameter whose type depends on a type parameter: its type is substituted from the
		//     receiver's (or method's) type arguments, so a synthesized default would be unified and
		//     validated against the substituted type, whereas a trailing omitted default never is.
		//   - A class metatype default (e.g. `cls = SomeClass`): the compiler deliberately keeps these
		//     out of the constant fast path and re-resolves them to the live compiled subclass, which a
		//     baked literal cannot do.
		// Until those interactions are designed, such a middle skip must be passed explicitly.
		if (!parameter->initializer->is_constant) {
			push_error(vformat(R"(Cannot skip parameter "%s": its default value is not a constant expression. Pass it explicitly.)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}
		if (_signature_type_involves_type_parameter(parameter->get_datatype())) {
			push_error(vformat(R"(Cannot skip parameter "%s": its default value depends on a type parameter. Pass it explicitly.)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}
		const GDScriptParser::DataType default_type = parameter->initializer->get_datatype();
		if (default_type.is_meta_type && default_type.kind == GDScriptParser::DataType::CLASS) {
			push_error(vformat(R"(Cannot skip parameter "%s": its default value is a class type that cannot be inlined. Pass it explicitly.)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		// Synthesize a constant argument from the parameter's default. Marking it constant routes it
		// through the normal constant-argument path in `validate_call_arg`, which applies the same
		// builtin-type conversion a written literal would receive.
		GDScriptParser::LiteralNode *constant_argument = parser->alloc_node<GDScriptParser::LiteralNode>();
		constant_argument->value = parameter->initializer->reduced_value;
		constant_argument->reduced = true;
		constant_argument->is_constant = true;
		constant_argument->reduced_value = parameter->initializer->reduced_value;
		constant_argument->set_datatype(parameter->initializer->get_datatype());
		slots.write[i] = constant_argument;
	}

	Vector<GDScriptParser::ExpressionNode *> canonical_arguments;
	for (int i = 0; i <= max_filled_index; i++) {
		canonical_arguments.push_back(slots[i]);
	}
	for (int i = 0; i < rest_arguments.size(); i++) {
		canonical_arguments.push_back(rest_arguments[i]);
	}

	p_call->arguments = canonical_arguments;
	p_call->argument_names.clear();
	return true;
}

void GDScriptAnalyzer::validate_call_arg(const MethodInfo &p_method, const GDScriptParser::CallNode *p_call) {
	List<GDScriptParser::DataType> arg_types;

	for (const PropertyInfo &E : p_method.arguments) {
		arg_types.push_back(type_from_property(E, true));
	}

#ifdef TOOLS_ENABLED
	// Cache the resolved parameter types for editor refactors (e.g. insert-explicit-cast),
	// matching the user-function call path. The analyzer owns the parsed tree, so writing
	// through the const handle is sound; the runtime compiler never reads this back.
	GDScriptParser::CallNode *mutable_call = const_cast<GDScriptParser::CallNode *>(p_call);
	mutable_call->resolved_parameter_types.clear();
	for (const GDScriptParser::DataType &arg_type : arg_types) {
		mutable_call->resolved_parameter_types.push_back(arg_type);
	}
#endif // TOOLS_ENABLED

	validate_call_arg(arg_types, p_method.default_arguments.size(), (p_method.flags & METHOD_FLAG_VARARG) != 0, p_call);
}

static bool _method_signature_accepts_argument_count(int p_argument_count, int p_parameter_count, int p_default_args_count, bool p_is_vararg, const Vector<int> &p_extra_allowed_argument_counts) {
	const int min_argument_count = p_parameter_count - p_default_args_count;
	if (p_argument_count >= min_argument_count && (p_is_vararg || p_argument_count <= p_parameter_count)) {
		return true;
	}

	for (int extra_argument_count : p_extra_allowed_argument_counts) {
		if (extra_argument_count == p_argument_count) {
			return true;
		}
	}

	return false;
}

String GDScriptAnalyzer::make_invalid_argument_error(const StringName &p_function, int p_argument_number, const GDScriptParser::DataType &p_expected_type, const GDScriptParser::DataType &p_actual_type, bool p_strict_dynamic_mismatch, bool p_strict_nullable_mismatch) const {
	if (p_strict_dynamic_mismatch) {
		return vformat(R"*(Cannot pass Variant value as argument %d of "%s()" in strict dynamic mode; expected "%s".)*",
				p_argument_number,
				p_function,
				p_expected_type.to_string());
	}
	if (p_strict_nullable_mismatch) {
		return vformat(R"*(Cannot pass nullable value of type "%s" as argument %d of "%s()"; expected non-nullable "%s".)*",
				p_actual_type.to_string(),
				p_argument_number,
				p_function,
				p_expected_type.to_string());
	}
	return vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*",
			p_function,
			p_argument_number,
			p_expected_type.to_string(),
			p_actual_type.to_string());
}

void GDScriptAnalyzer::validate_call_arg(const List<GDScriptParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const GDScriptParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count) {
	if (p_call->arguments.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, p_par_types.size() - p_default_args_count, p_call->arguments.size()), p_call);
	}
	if (!p_is_vararg && p_call->arguments.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, p_par_types.size(), p_call->arguments.size()), p_call->arguments[p_par_types.size()]);
	}

	List<GDScriptParser::DataType>::ConstIterator par_itr = p_par_types.begin();
	const int checked_argument_count = MAX(p_call->arguments.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; ++par_itr, ++i) {
		if (i >= p_par_types.size()) {
			// Already on vararg place.
			break;
		}
		GDScriptParser::DataType par_type = *par_itr;

		if (par_type.is_hard_type() && p_call->arguments[i]->is_constant) {
			update_const_expression_builtin_type(p_call->arguments[i], par_type, "pass");
		}
		GDScriptParser::DataType arg_type = p_call->arguments[i]->get_datatype();

		if (arg_type.is_variant() || !arg_type.is_hard_type()) {
			if (arg_type.is_variant() && strict_dynamic_checks && !(par_type.is_hard_type() && par_type.is_variant())) {
				push_error(make_invalid_argument_error(p_call->function_name, i + 1, par_type, arg_type, true, false), p_call->arguments[i]);
			} else {
#ifdef DEBUG_ENABLED
				// Argument can be anything, so this is unsafe (unless the parameter is a hard variant).
				if (!(par_type.is_hard_type() && par_type.is_variant())) {
					mark_node_unsafe(p_call->arguments[i]);
					parser->push_warning(p_call->arguments[i], GDScriptWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "function", p_call->function_name, par_type.to_string(), arg_type.to_string_strict());
				}
#endif // DEBUG_ENABLED
			}
		} else if (par_type.is_hard_type() && !is_type_compatible(par_type, arg_type, true)) {
			const bool nullable_mismatch = strict_null_checks && arg_type.is_nullable && !par_type.is_nullable && !par_type.is_variant();
			if (nullable_mismatch || !is_type_compatible(arg_type, par_type)) {
				push_error(make_invalid_argument_error(p_call->function_name, i + 1, par_type, arg_type, false, nullable_mismatch), p_call->arguments[i]);
#ifdef DEBUG_ENABLED
			} else {
				// Supertypes are acceptable for dynamic compliance, but it's unsafe.
				mark_node_unsafe(p_call);
				parser->push_warning(p_call->arguments[i], GDScriptWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "function", p_call->function_name, par_type.to_string(), arg_type.to_string_strict());
#endif // DEBUG_ENABLED
			}
#ifdef DEBUG_ENABLED
		} else if (par_type.kind == GDScriptParser::DataType::BUILTIN && par_type.builtin_type == Variant::INT && arg_type.kind == GDScriptParser::DataType::BUILTIN && arg_type.builtin_type == Variant::FLOAT) {
			parser->push_warning(p_call->arguments[i], GDScriptWarning::NARROWING_CONVERSION, p_call->function_name);
#endif // DEBUG_ENABLED
		}
	}
}

void GDScriptAnalyzer::validate_callable_array_literal_args(const Vector<GDScriptParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, GDScriptParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count) {
	if (p_array == nullptr) {
		return;
	}

	if (p_array->elements.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_function, p_par_types.size() - p_default_args_count, p_array->elements.size()), p_array);
	}
	if (!p_is_vararg && p_array->elements.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_function, p_par_types.size(), p_array->elements.size()), p_array->elements[p_par_types.size()]);
	}

	const int checked_argument_count = MAX(p_array->elements.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; i++) {
		if (i >= p_par_types.size()) {
			break;
		}

		GDScriptParser::ExpressionNode *argument = p_array->elements[i];
		GDScriptParser::DataType par_type = p_par_types[i];

		if (par_type.is_hard_type() && argument->is_constant) {
			update_const_expression_builtin_type(argument, par_type, "pass");
		}
		GDScriptParser::DataType arg_type = argument->get_datatype();

		if (arg_type.is_variant() || !arg_type.is_hard_type()) {
			if (arg_type.is_variant() && strict_dynamic_checks && !(par_type.is_hard_type() && par_type.is_variant())) {
				push_error(make_invalid_argument_error(p_function, i + 1, par_type, arg_type, true, false), argument);
			} else {
#ifdef DEBUG_ENABLED
				if (!(par_type.is_hard_type() && par_type.is_variant())) {
					mark_node_unsafe(argument);
					parser->push_warning(argument, GDScriptWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "function", p_function, par_type.to_string(), arg_type.to_string_strict());
				}
#endif // DEBUG_ENABLED
			}
		} else if (par_type.is_hard_type() && !is_type_compatible(par_type, arg_type, true)) {
			const bool nullable_mismatch = strict_null_checks && arg_type.is_nullable && !par_type.is_nullable && !par_type.is_variant();
			if (nullable_mismatch || !is_type_compatible(arg_type, par_type)) {
				push_error(make_invalid_argument_error(p_function, i + 1, par_type, arg_type, false, nullable_mismatch), argument);
#ifdef DEBUG_ENABLED
			} else {
				mark_node_unsafe(argument);
				parser->push_warning(argument, GDScriptWarning::UNSAFE_CALL_ARGUMENT, itos(i + 1), "function", p_function, par_type.to_string(), arg_type.to_string_strict());
#endif // DEBUG_ENABLED
			}
#ifdef DEBUG_ENABLED
		} else if (par_type.kind == GDScriptParser::DataType::BUILTIN && par_type.builtin_type == Variant::INT && arg_type.kind == GDScriptParser::DataType::BUILTIN && arg_type.builtin_type == Variant::FLOAT) {
			parser->push_warning(argument, GDScriptWarning::NARROWING_CONVERSION, p_function);
#endif // DEBUG_ENABLED
		}
	}
}

GDScriptParser::DataType GDScriptAnalyzer::explicit_signal_type_from_info(const MethodInfo &p_info) const {
	GDScriptParser::DataType signal_type = make_signal_type(p_info);
	signal_type.method_parameter_types.clear();
	for (const PropertyInfo &argument : p_info.arguments) {
		signal_type.method_parameter_types.push_back(type_from_property(argument, true));
	}
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

GDScriptParser::DataType GDScriptAnalyzer::explicit_signal_type_from_node(const GDScriptParser::SignalNode *p_signal) const {
	GDScriptParser::DataType signal_type = p_signal->get_datatype();
	signal_type.method_parameter_types.clear();
	for (GDScriptParser::ParameterNode *parameter : p_signal->parameters) {
		signal_type.method_parameter_types.push_back(parameter->get_datatype());
	}
	signal_type.has_method_signature = true;
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

bool GDScriptAnalyzer::signal_name_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const {
	if (p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return false;
	}

	const GDScriptParser::ExpressionNode *signal_arg = p_call->arguments[p_signal_arg_index];
	if (signal_arg == nullptr || !signal_arg->is_constant) {
		return false;
	}

	const Variant signal_name_value = signal_arg->reduced_value;
	if (signal_name_value.get_type() != Variant::STRING && signal_name_value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	r_signal_name = signal_name_value;
	return true;
}

bool GDScriptAnalyzer::signal_type_from_receiver(const GDScriptParser::DataType &p_receiver_type, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const {
	if (p_receiver_type.kind == GDScriptParser::DataType::CLASS) {
		return signal_type_from_class_constant_arg(p_receiver_type.class_type, p_call, p_signal_arg_index, r_signal_type);
	}
	if (p_receiver_type.kind == GDScriptParser::DataType::NATIVE) {
		return signal_type_from_native_constant_arg(p_receiver_type.native_type, p_call, p_signal_arg_index, r_signal_type);
	}
	return false;
}

bool GDScriptAnalyzer::signal_type_from_class_constant_arg(const GDScriptParser::ClassNode *p_class, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const {
	if (p_class == nullptr) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	for (const GDScriptParser::ClassNode *class_node = p_class; class_node != nullptr;) {
		if (class_node->has_member(signal_name)) {
			const GDScriptParser::ClassNode::Member &member = class_node->get_member(signal_name);
			if (member.type != GDScriptParser::ClassNode::Member::SIGNAL) {
				return false;
			}

			r_signal_type = explicit_signal_type_from_node(member.signal);
			return true;
		}

		if (class_node->base_type.kind == GDScriptParser::DataType::CLASS) {
			class_node = class_node->base_type.class_type;
		} else if (class_node->base_type.kind == GDScriptParser::DataType::NATIVE) {
			return signal_type_from_native_constant_arg(class_node->base_type.native_type, p_call, p_signal_arg_index, r_signal_type);
		} else {
			class_node = nullptr;
		}
	}

	return false;
}

bool GDScriptAnalyzer::signal_type_from_native_constant_arg(const StringName &p_native_type, const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const {
	if (p_native_type == StringName()) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	MethodInfo signal_info;
	if (!ClassDB::get_signal(p_native_type, signal_name, &signal_info)) {
		return false;
	}

	r_signal_type = explicit_signal_type_from_info(signal_info);
	return true;
}

bool GDScriptAnalyzer::local_signal_type_from_constant_arg(const GDScriptParser::CallNode *p_call, int p_signal_arg_index, GDScriptParser::DataType &r_signal_type) const {
	return signal_type_from_class_constant_arg(parser->current_class, p_call, p_signal_arg_index, r_signal_type);
}

void GDScriptAnalyzer::validate_strict_signal_name_fallback(const GDScriptParser::CallNode *p_call, const GDScriptParser::DataType &p_receiver_type, int p_signal_arg_index) {
	if (!strict_dynamic_checks || p_call == nullptr || p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return;
	}

	StringName signal_name;
	if (signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		push_error(vformat(R"*(Cannot resolve signal "%s" on type "%s" for "%s()" in strict dynamic mode.)*",
						   signal_name,
						   p_receiver_type.to_string(),
						   p_call->function_name),
				p_call->arguments[p_signal_arg_index]);
	} else if (call_argument_can_be_string_name(p_call, p_signal_arg_index)) {
		push_error(vformat(R"*(Cannot use dynamic signal name for "%s()" on type "%s" in strict dynamic mode.)*",
						   p_call->function_name,
						   p_receiver_type.to_string()),
				p_call->arguments[p_signal_arg_index]);
	}
}

void GDScriptAnalyzer::validate_signal_connect_arg(const GDScriptParser::DataType &p_signal_type, const GDScriptParser::CallNode *p_call, int p_callable_arg_index, bool p_require_explicit_signal) {
	if ((p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_callable_arg_index < 0 || p_callable_arg_index >= p_call->arguments.size()) {
		return;
	}
	if (p_signal_type.kind != GDScriptParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}
	if (p_require_explicit_signal && !p_signal_type.has_explicit_method_signature) {
		return;
	}

	const GDScriptParser::DataType callable_type = p_call->arguments[p_callable_arg_index]->get_datatype();
	Vector<GDScriptParser::DataType> callable_parameter_types;
	int callable_default_arg_count = 0;
	bool callable_is_vararg = false;
	if (!callable_signature_from_type(callable_type, callable_parameter_types, callable_default_arg_count, callable_is_vararg)) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int callable_argument_count = callable_parameter_types.size();
	const int callable_min_argument_count = callable_argument_count - callable_default_arg_count;
	const StringName action_name = p_call->function_name == SNAME("disconnect") ? SNAME("disconnect") : (p_call->function_name == SNAME("is_connected") ? StringName("check connection for") : SNAME("connect"));
	const String callable_type_string = callable_type_string_with_signature(callable_type, callable_parameter_types);
	if (!_method_signature_accepts_argument_count(signal_argument_count, callable_argument_count, callable_default_arg_count, callable_is_vararg, callable_type.method_extra_allowed_argument_counts)) {
		push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal emits %d arguments but callable expects %s%d.)*",
						   action_name,
						   p_signal_type.to_string(),
						   callable_type_string,
						   signal_argument_count,
						   callable_default_arg_count > 0 ? "at least " : "",
						   callable_default_arg_count > 0 ? callable_min_argument_count : callable_argument_count),
				p_call->arguments[p_callable_arg_index]);
		return;
	}

	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = true;
	options.strict_null = strict_null_checks;

	const int checked_signal_argument_count = MAX(signal_argument_count - callable_type.method_unbound_argument_count, 0);
	for (int i = 0; i < checked_signal_argument_count && i < callable_argument_count; i++) {
		const GDScriptParser::DataType &callable_parameter_type = callable_parameter_types[i];
		const GDScriptParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		const bool nullable_mismatch = strict_null_checks && signal_parameter_type.is_nullable && !callable_parameter_type.is_nullable && !callable_parameter_type.is_variant();
		if (nullable_mismatch || !GDScriptTypeCompatibility::check(callable_parameter_type, signal_parameter_type, options).compatible) {
			if (nullable_mismatch) {
				push_error(vformat("Cannot %s signal \"%s\" to callable \"%s\": signal argument %d is nullable "
								   "type \"%s\", but callable parameter expects non-nullable \"%s\".",
								   action_name,
								   p_signal_type.to_string(),
								   callable_type_string,
								   i + 1,
								   signal_parameter_type.to_string(),
								   callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			} else {
				push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal argument %d of type "%s" cannot be passed to callable parameter of type "%s".)*",
								   action_name,
								   p_signal_type.to_string(),
								   callable_type_string,
								   i + 1,
								   signal_parameter_type.to_string(),
								   callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			}
			return;
		}
	}
}

void GDScriptAnalyzer::validate_signal_emit_args(const GDScriptParser::DataType &p_signal_type, const GDScriptParser::CallNode *p_call, int p_first_emit_arg_index) {
	if (p_signal_type.kind != GDScriptParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int emit_argument_count = p_call->arguments.size() - p_first_emit_arg_index;
	if (emit_argument_count < signal_argument_count) {
		push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call);
		return;
	}
	if (emit_argument_count > signal_argument_count) {
		push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call->arguments[signal_argument_count + p_first_emit_arg_index]);
		return;
	}

#ifdef TOOLS_ENABLED
	// Refine the per-payload parameter types cached for editor refactors (insert-explicit-cast).
	// The generic `Object.emit_signal` vararg signature recorded Variant payload slots; overwrite
	// them with the resolved signal parameter types, aligned to the call's actual argument indices
	// (the leading name argument occupies the slots before `p_first_emit_arg_index`). The analyzer
	// owns the parsed tree, so writing through the const handle is sound; the runtime never reads it.
	GDScriptParser::CallNode *mutable_call = const_cast<GDScriptParser::CallNode *>(p_call);
	if (mutable_call->resolved_parameter_types.size() < p_first_emit_arg_index + signal_argument_count) {
		mutable_call->resolved_parameter_types.resize(p_first_emit_arg_index + signal_argument_count);
	}
	for (int i = 0; i < signal_argument_count; i++) {
		mutable_call->resolved_parameter_types.write[p_first_emit_arg_index + i] = p_signal_type.method_parameter_types[i];
	}
#endif // TOOLS_ENABLED

	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = strict_dynamic_checks;
	options.strict_null = strict_null_checks;

	for (int i = 0; i < signal_argument_count; i++) {
		const int emit_argument_index = p_first_emit_arg_index + i;
		const GDScriptParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		const GDScriptParser::DataType emit_argument_type = p_call->arguments[emit_argument_index]->get_datatype();

		if (emit_argument_type.has_no_type()) {
			mark_node_unsafe(p_call->arguments[emit_argument_index]);
			continue;
		}
		if (emit_argument_type.is_variant() || !emit_argument_type.is_hard_type()) {
			if (emit_argument_type.is_variant() && strict_dynamic_checks && !(signal_parameter_type.is_hard_type() && signal_parameter_type.is_variant())) {
				push_error(make_invalid_argument_error(p_call->function_name, emit_argument_index + 1, signal_parameter_type, emit_argument_type, true, false), p_call->arguments[emit_argument_index]);
			} else {
				mark_node_unsafe(p_call->arguments[emit_argument_index]);
			}
			continue;
		}

		const bool nullable_mismatch = strict_null_checks && emit_argument_type.is_nullable && !signal_parameter_type.is_nullable && !signal_parameter_type.is_variant();
		if (nullable_mismatch || !GDScriptTypeCompatibility::check(signal_parameter_type, emit_argument_type, options).compatible) {
			push_error(make_invalid_argument_error(p_call->function_name, emit_argument_index + 1, signal_parameter_type, emit_argument_type, false, nullable_mismatch), p_call->arguments[emit_argument_index]);
			return;
		}
	}
}

void GDScriptAnalyzer::validate_local_object_signal_callable_arg(const GDScriptParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_call->arguments.size() < 2) {
		return;
	}

	GDScriptParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, parser->current_class->get_datatype(), 0);
		return;
	}

	validate_signal_connect_arg(signal_type, p_call, 1, false);
}

void GDScriptAnalyzer::validate_local_object_emit_signal_args(const GDScriptParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || p_call->function_name != SNAME("emit_signal") || p_call->arguments.is_empty()) {
		return;
	}

	GDScriptParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, parser->current_class->get_datatype(), 0);
		return;
	}

	validate_signal_emit_args(signal_type, p_call, 1);
}

void GDScriptAnalyzer::validate_typed_object_signal_api_args(const GDScriptParser::DataType &p_base_type, const GDScriptParser::CallNode *p_call, bool p_is_self) {
	if (p_is_self) {
		return;
	}

	if (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected") && p_call->function_name != SNAME("emit_signal")) {
		return;
	}

	if (p_base_type.kind != GDScriptParser::DataType::CLASS && p_base_type.kind != GDScriptParser::DataType::NATIVE) {
		return;
	}

	GDScriptParser::DataType signal_type;
	if (!signal_type_from_receiver(p_base_type, p_call, 0, signal_type)) {
		validate_strict_signal_name_fallback(p_call, p_base_type, 0);
		return;
	}

	if (p_call->function_name == SNAME("emit_signal")) {
		validate_signal_emit_args(signal_type, p_call, 1);
	} else {
		validate_signal_connect_arg(signal_type, p_call, 1, false);
	}
}

#ifdef DEBUG_ENABLED
void GDScriptAnalyzer::is_shadowing(GDScriptParser::IdentifierNode *p_identifier, const String &p_context, const bool p_in_local_scope) {
	const StringName &name = p_identifier->name;

	{
		List<MethodInfo> gdscript_funcs;
		GDScriptLanguage::get_singleton()->get_public_functions(&gdscript_funcs);

		for (MethodInfo &info : gdscript_funcs) {
			if (info.name == name) {
				parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in function");
				return;
			}
		}
		if (Variant::has_utility_function(name)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in function");
			return;
		} else if (class_exists(name)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "native class");
			return;
		} else if (ScriptServer::is_global_class(name)) {
			String class_path = ScriptServer::get_global_class_path(name).get_file();
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, vformat(R"(global class defined in "%s")", class_path));
			return;
		} else if (GDScriptParser::get_builtin_type(name) < Variant::VARIANT_MAX) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_GLOBAL_IDENTIFIER, p_context, name, "built-in type");
			return;
		}
	}

	const GDScriptParser::DataType current_class_type = parser->current_class->get_datatype();
	if (p_in_local_scope) {
		GDScriptParser::ClassNode *base_class = current_class_type.class_type;

		if (base_class != nullptr) {
			if (base_class->has_member(name)) {
				parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE, p_context, p_identifier->name, base_class->get_member(name).get_type_name(), itos(base_class->get_member(name).get_line()));
				return;
			}
			base_class = base_class->base_type.class_type;
		}

		while (base_class != nullptr) {
			if (base_class->has_member(name)) {
				String base_class_name = base_class->get_global_name();
				if (base_class_name.is_empty()) {
					base_class_name = base_class->fqcn;
				}

				parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, base_class->get_member(name).get_type_name(), itos(base_class->get_member(name).get_line()), base_class_name);
				return;
			}
			base_class = base_class->base_type.class_type;
		}
	}

	StringName native_base_class = current_class_type.native_type;
	while (native_base_class != StringName()) {
		ERR_FAIL_COND_MSG(!class_exists(native_base_class), "Non-existent native base class.");

		if (ClassDB::has_method(native_base_class, name, true)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "method", native_base_class);
			return;
		} else if (ClassDB::has_signal(native_base_class, name, true)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "signal", native_base_class);
			return;
		} else if (ClassDB::has_property(native_base_class, name, true)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "property", native_base_class);
			return;
		} else if (ClassDB::has_integer_constant(native_base_class, name, true)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "constant", native_base_class);
			return;
		} else if (ClassDB::has_enum(native_base_class, name, true)) {
			parser->push_warning(p_identifier, GDScriptWarning::SHADOWED_VARIABLE_BASE_CLASS, p_context, p_identifier->name, "enum", native_base_class);
			return;
		}
		native_base_class = ClassDB::get_parent_class(native_base_class);
	}
}
#endif // DEBUG_ENABLED

GDScriptParser::DataType GDScriptAnalyzer::get_operation_type(Variant::Operator p_operation, const GDScriptParser::DataType &p_a, bool &r_valid, const GDScriptParser::Node *p_source) {
	// Unary version.
	GDScriptParser::DataType nil_type;
	nil_type.builtin_type = Variant::NIL;
	nil_type.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
	return get_operation_type(p_operation, p_a, nil_type, r_valid, p_source);
}

GDScriptParser::DataType GDScriptAnalyzer::get_operation_type(Variant::Operator p_operation, const GDScriptParser::DataType &p_a, const GDScriptParser::DataType &p_b, bool &r_valid, const GDScriptParser::Node *p_source) {
	if (p_operation == Variant::OP_AND || p_operation == Variant::OP_OR) {
		// Those work for any type of argument and always return a boolean.
		// They don't use the Variant operator since they have short-circuit semantics.
		r_valid = true;
		GDScriptParser::DataType result;
		result.type_source = GDScriptParser::DataType::ANNOTATED_INFERRED;
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
		return result;
	}

	Variant::Type a_type = p_a.builtin_type;
	Variant::Type b_type = p_b.builtin_type;

	if (p_a.kind == GDScriptParser::DataType::ENUM) {
		if (p_a.is_meta_type) {
			a_type = Variant::DICTIONARY;
		} else {
			a_type = Variant::INT;
		}
	}
	if (p_b.kind == GDScriptParser::DataType::ENUM) {
		if (p_b.is_meta_type) {
			b_type = Variant::DICTIONARY;
		} else {
			b_type = Variant::INT;
		}
	}

	GDScriptParser::DataType result;
	bool hard_operation = p_a.is_hard_type() && p_b.is_hard_type();

	if (p_operation == Variant::OP_ADD && a_type == Variant::ARRAY && b_type == Variant::ARRAY) {
		if (p_a.has_container_element_type(0) && p_b.has_container_element_type(0)) {
			if (p_a.get_container_element_type(0) == p_b.get_container_element_type(0)) {
				r_valid = true;
				result = p_a;
				result.type_source = hard_operation ? GDScriptParser::DataType::ANNOTATED_INFERRED : GDScriptParser::DataType::INFERRED;
				return result;
			}

			r_valid = false;
			result.kind = GDScriptParser::DataType::BUILTIN;
			result.builtin_type = Variant::ARRAY;
			result.type_source = hard_operation ? GDScriptParser::DataType::ANNOTATED_INFERRED : GDScriptParser::DataType::INFERRED;
			return result;
		}
	}

	Variant::ValidatedOperatorEvaluator op_eval = Variant::get_validated_operator_evaluator(p_operation, a_type, b_type);
	bool validated = op_eval != nullptr;

	if (validated) {
		r_valid = true;
		result.type_source = hard_operation ? GDScriptParser::DataType::ANNOTATED_INFERRED : GDScriptParser::DataType::INFERRED;
		result.kind = GDScriptParser::DataType::BUILTIN;
		result.builtin_type = Variant::get_operator_return_type(p_operation, a_type, b_type);
	} else {
		r_valid = !hard_operation;
		result.kind = GDScriptParser::DataType::VARIANT;
	}

	return result;
}

bool GDScriptAnalyzer::is_type_compatible(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, bool p_allow_implicit_conversion, const GDScriptParser::Node *p_source_node) {
#ifdef DEBUG_ENABLED
	if (p_source_node) {
		if (p_target.kind == GDScriptParser::DataType::ENUM) {
			if (p_source.kind == GDScriptParser::DataType::BUILTIN && p_source.builtin_type == Variant::INT) {
				parser->push_warning(p_source_node, GDScriptWarning::INT_AS_ENUM_WITHOUT_CAST);
			}
		}
	}
#endif // DEBUG_ENABLED
	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	options.strict_dynamic = strict_dynamic_checks;
	options.strict_null = strict_null_checks;
	return GDScriptTypeCompatibility::check(p_target, p_source, options).compatible;
}

bool GDScriptAnalyzer::check_type_compatibility(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, bool p_allow_implicit_conversion, const GDScriptParser::Node *p_source_node) {
	(void)p_source_node;
	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	return GDScriptTypeCompatibility::check(p_target, p_source, options).compatible;
}

void GDScriptAnalyzer::push_error(const String &p_message, const GDScriptParser::Node *p_origin) {
	mark_node_unsafe(p_origin);
	parser->push_error(p_message, p_origin);
}

void GDScriptAnalyzer::mark_node_unsafe(const GDScriptParser::Node *p_node) {
#ifdef DEBUG_ENABLED
	if (p_node == nullptr) {
		return;
	}

	for (int i = p_node->start_line; i <= p_node->end_line; i++) {
		parser->unsafe_lines.insert(i);
	}
#endif // DEBUG_ENABLED
}

void GDScriptAnalyzer::downgrade_node_type_source(GDScriptParser::Node *p_node) {
	GDScriptParser::IdentifierNode *identifier = nullptr;
	if (p_node->type == GDScriptParser::Node::IDENTIFIER) {
		identifier = static_cast<GDScriptParser::IdentifierNode *>(p_node);
	} else if (p_node->type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_node);
		if (subscript->is_attribute) {
			identifier = subscript->attribute;
		}
	}
	if (identifier == nullptr) {
		return;
	}

	GDScriptParser::Node *source = nullptr;
	switch (identifier->source) {
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE: {
			source = identifier->variable_source;
		} break;
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER: {
			source = identifier->parameter_source;
		} break;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE: {
			source = identifier->variable_source;
		} break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR: {
			source = identifier->bind_source;
		} break;
		default:
			break;
	}
	if (source == nullptr) {
		return;
	}

	GDScriptParser::DataType datatype;
	datatype.kind = GDScriptParser::DataType::VARIANT;
	source->set_datatype(datatype);
}

void GDScriptAnalyzer::mark_lambda_use_self() {
	GDScriptParser::LambdaNode *lambda = current_lambda;
	while (lambda != nullptr) {
		lambda->use_self = true;
		lambda = lambda->parent_lambda;
	}
}

void GDScriptAnalyzer::resolve_pending_lambda_bodies() {
	if (pending_body_resolution_lambdas.is_empty()) {
		return;
	}

	GDScriptParser::LambdaNode *previous_lambda = current_lambda;
	bool previous_static_context = static_context;

	List<GDScriptParser::LambdaNode *> lambdas = pending_body_resolution_lambdas;
	pending_body_resolution_lambdas.clear();

	for (GDScriptParser::LambdaNode *lambda : lambdas) {
		current_lambda = lambda;
		static_context = lambda->function->is_static;

		resolve_function_body(lambda->function, true);

		int captures_amount = lambda->captures.size();
		if (captures_amount > 0) {
			// Create space for lambda parameters.
			// At the beginning to not mess with optional parameters.
			int param_count = lambda->function->parameters.size();
			lambda->function->parameters.resize(param_count + captures_amount);
			for (int i = param_count - 1; i >= 0; i--) {
				lambda->function->parameters.write[i + captures_amount] = lambda->function->parameters[i];
				lambda->function->parameters_indices[lambda->function->parameters[i]->identifier->name] = i + captures_amount;
			}

			// Add captures as extra parameters at the beginning.
			for (int i = 0; i < lambda->captures.size(); i++) {
				GDScriptParser::IdentifierNode *capture = lambda->captures[i];
				GDScriptParser::ParameterNode *capture_param = parser->alloc_node<GDScriptParser::ParameterNode>();
				capture_param->identifier = capture;
				capture_param->usages = capture->usages;
				capture_param->set_datatype(capture->get_datatype());

				lambda->function->parameters.write[i] = capture_param;
				lambda->function->parameters_indices[capture->name] = i;
			}
		}
	}

	current_lambda = previous_lambda;
	static_context = previous_static_context;
}

bool GDScriptAnalyzer::class_exists(const StringName &p_class) {
	return ClassDB::class_exists(p_class) && ClassDB::is_class_exposed(p_class);
}

Error GDScriptAnalyzer::resolve_inheritance() {
	return resolve_class_inheritance(parser->head, true);
}

Error GDScriptAnalyzer::resolve_interface() {
	Error err = resolve_trait_uses(parser->head, true);
	if (err) {
		return err;
	}

	resolve_class_interface(parser->head, true);
	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

Error GDScriptAnalyzer::resolve_body() {
	resolve_class_body(parser->head, true);

#ifdef DEBUG_ENABLED
	// Apply here, after all `@warning_ignore`s have been resolved and applied.
	parser->apply_pending_warnings();
#endif // DEBUG_ENABLED

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

Error GDScriptAnalyzer::resolve_dependencies() {
	for (KeyValue<String, Ref<GDScriptParserRef>> &K : parser->depended_parsers) {
		if (K.value.is_null()) {
			return ERR_PARSE_ERROR;
		}
		K.value->raise_status(GDScriptParserRef::INHERITANCE_SOLVED);
	}

	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

Error GDScriptAnalyzer::analyze() {
	parser->errors.clear();

	Error err = validate_imports();
	if (err) {
		return err;
	}

	err = validate_annotation_declarations();
	if (err) {
		return err;
	}

#ifdef DEBUG_ENABLED
	validate_mixed_namespace_directory();
#endif // DEBUG_ENABLED

	err = resolve_inheritance();
	if (err) {
		return err;
	}

	resolve_interface();
	err = resolve_body();
	if (err) {
		return err;
	}

	return resolve_dependencies();
}

GDScriptAnalyzer::GDScriptAnalyzer(GDScriptParser *p_parser) {
	parser = p_parser;
	strict_null_checks = GLOBAL_GET_CACHED(bool, "debug/gdscript/analysis/strict_null_checks");
	strict_dynamic_checks = GLOBAL_GET_CACHED(bool, "debug/gdscript/analysis/strict_dynamic_checks");
}
