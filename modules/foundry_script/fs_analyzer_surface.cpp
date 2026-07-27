/**************************************************************************/
/*  fs_analyzer_surface.cpp                                               */
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

#include "fs_analyzer.h"

#include "foundry_script.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

#define UNNAMED_ENUM "<anonymous enum>"
#define ENUM_SEPARATOR "."

static String _class_or_trait_name(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	return p_class->fqcn.get_file();
}

static FSParser::DataType make_signal_type(const MethodInfo &p_info) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = Variant::SIGNAL;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	return type;
}

static FSParser::DataType make_signal_type(const MethodInfo &p_info, const FSParser::SignalNode *p_signal) {
	FSParser::DataType type = make_signal_type(p_info);
	for (const FSParser::ParameterNode *parameter : p_signal->parameters) {
		type.method_parameter_types.push_back(parameter->get_datatype());
	}
	return type;
}

static FSParser::DataType make_enum_type(const StringName &p_enum_name, const String &p_base_name, const bool p_meta = false) {
	FSParser::DataType type;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = FSParser::DataType::ENUM;
	type.builtin_type = p_meta ? Variant::DICTIONARY : Variant::INT;
	type.enum_type = p_enum_name;
	type.is_constant = true;
	type.is_meta_type = p_meta;

	if (p_base_name.is_empty()) {
		type.native_type = p_enum_name;
	} else {
		type.native_type = p_base_name + ENUM_SEPARATOR + p_enum_name;
	}

	return type;
}

static FSParser::DataType make_class_enum_type(const StringName &p_enum_name, FSParser::ClassNode *p_class, const String &p_script_path, bool p_meta = true) {
	FSParser::DataType type = make_enum_type(p_enum_name, p_class->fqcn, p_meta);

	type.class_type = p_class;
	type.script_path = p_script_path;

	return type;
}

static const FSParser::Node *_trait_use_source(const FSParser::ClassNode::TraitUse &p_trait_use,
		const FSParser::ClassNode *p_owner) {
	if (!p_trait_use.name.is_empty()) {
		return p_trait_use.name[0];
	}
	return p_owner;
}

static void _append_trait_unique(Vector<FSParser::ClassNode *> &r_traits, FSParser::ClassNode *p_trait) {
	if (p_trait != nullptr && !r_traits.has(p_trait)) {
		r_traits.push_back(p_trait);
	}
}

// Tuples and tagged unions both erase to Array at runtime and have no inspector representation
// in v1 (`@export` of either is a v1-deferred feature, not just of the top-level property type).
// This walks into `Array`/`Dictionary` container element types so `Array[Message]` or
// `Dictionary[String, (int, int)]` are caught too, not just a bare `Message` or `(int, int)`
// property. `r_found_type` receives the offending element type for diagnostics.
static bool _export_type_contains_tuple_or_tagged_union(const FSParser::DataType &p_type, FSParser::DataType &r_found_type) {
	// A tagged union's own metatype (e.g. `@export var x = Message`, exported as a Dictionary of
	// case tags, same as a plain int-backed enum) is not a tagged-union *value* and stays supported.
	const bool is_rejected_tagged_union_value = p_type.is_tagged_union_type() && !p_type.is_meta_type;
	if (p_type.is_tuple() || is_rejected_tagged_union_value) {
		r_found_type = p_type;
		return true;
	}
	if (p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
		return _export_type_contains_tuple_or_tagged_union(p_type.get_container_element_type(0), r_found_type);
	}
	if (p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
		if (_export_type_contains_tuple_or_tagged_union(p_type.get_container_element_type_or_variant(0), r_found_type)) {
			return true;
		}
		if (_export_type_contains_tuple_or_tagged_union(p_type.get_container_element_type_or_variant(1), r_found_type)) {
			return true;
		}
	}
	return false;
}

static bool _datatype_alpha_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b) {
	if (!(p_a == p_b)) {
		return false;
	}
	if (p_a.has_method_signature != p_b.has_method_signature) {
		return false;
	}
	if (p_a.signature_is_async != p_b.signature_is_async) {
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

bool FSAnalyzer::has_member_name_conflict_in_script_class(const StringName &p_member_name, const FSParser::ClassNode *p_class, const FSParser::Node *p_member) {
	if (p_class->members_indices.has(p_member_name)) {
		int index = p_class->members_indices[p_member_name];
		const FSParser::ClassNode::Member *member = &p_class->members[index];

		if (member->type == FSParser::ClassNode::Member::VARIABLE ||
				member->type == FSParser::ClassNode::Member::CONSTANT ||
				member->type == FSParser::ClassNode::Member::ENUM ||
				member->type == FSParser::ClassNode::Member::ENUM_VALUE ||
				member->type == FSParser::ClassNode::Member::CLASS ||
				member->type == FSParser::ClassNode::Member::SIGNAL) {
			return true;
		}
		if (p_member->type != FSParser::Node::FUNCTION && member->type == FSParser::ClassNode::Member::FUNCTION) {
			return true;
		}
	}

	return false;
}

static bool _member_is_visible_outer_class_surface(const FSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case FSParser::ClassNode::Member::CONSTANT:
		case FSParser::ClassNode::Member::ENUM:
		case FSParser::ClassNode::Member::ENUM_VALUE:
		case FSParser::ClassNode::Member::CLASS:
		case FSParser::ClassNode::Member::TUPLE:
			return true;
		case FSParser::ClassNode::Member::VARIABLE:
		case FSParser::ClassNode::Member::FUNCTION:
		case FSParser::ClassNode::Member::SIGNAL:
		case FSParser::ClassNode::Member::GROUP:
		case FSParser::ClassNode::Member::UNDEFINED:
			return false;
	}

	return false;
}

bool FSAnalyzer::has_member_name_conflict_in_native_type(const StringName &p_member_name, const StringName &p_native_type_string) {
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

Error FSAnalyzer::check_native_member_name_conflict(const StringName &p_member_name, const FSParser::Node *p_member_node, const StringName &p_native_type_string) {
	if (has_member_name_conflict_in_native_type(p_member_name, p_native_type_string)) {
		push_error(vformat(R"(Member "%s" redefined (original in native class '%s'))", p_member_name, p_native_type_string), p_member_node);
		return ERR_PARSE_ERROR;
	}

	if (class_exists(p_member_name)) {
		push_error(vformat(R"(The member "%s" shadows a native class.)", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}

	if (FSParser::get_builtin_type(p_member_name) < Variant::VARIANT_MAX || p_member_name == SNAME("AsyncCallable")) {
		push_error(vformat(R"(The member "%s" cannot have the same name as a builtin type.)", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}

	return OK;
}

Error FSAnalyzer::check_outer_class_member_name_conflict(const FSParser::ClassNode *p_class_node, const StringName &p_member_name, const FSParser::Node *p_member_node) {
	if (p_class_node->outer == nullptr) {
		return OK;
	}

	// Match class-scope lookup order for the lexical outer portion only: an outer class, its script
	// bases, then the next lexical outer class. Outer static variables/functions are intentionally
	// excluded because bare lookup does not expose them from inner classes today.
	List<FSParser::ClassNode *> outer_scope_classes;
	get_class_node_current_scope_classes(p_class_node->outer, &outer_scope_classes, const_cast<FSParser::Node *>(p_member_node));

	for (FSParser::ClassNode *outer_class_node : outer_scope_classes) {
		if (outer_class_node == nullptr) {
			continue;
		}

		if (outer_class_node->identifier != nullptr && outer_class_node->identifier->name == p_member_name) {
			push_error(vformat(R"(The member "%s" already exists in outer class %s.)", p_member_name, _class_or_trait_name(outer_class_node)), p_member_node);
			return ERR_PARSE_ERROR;
		}

		if (!outer_class_node->members_indices.has(p_member_name)) {
			continue;
		}

		const int member_index = outer_class_node->members_indices[p_member_name];
		const FSParser::ClassNode::Member &outer_member = outer_class_node->members[member_index];
		if (!_member_is_visible_outer_class_surface(outer_member)) {
			continue;
		}

		if (has_member_name_conflict_in_script_class(p_member_name, outer_class_node, p_member_node)) {
			push_error(vformat(R"(The member "%s" already exists in outer class %s.)", p_member_name, _class_or_trait_name(outer_class_node)), p_member_node);
			return ERR_PARSE_ERROR;
		}
	}

	return OK;
}

Error FSAnalyzer::check_class_member_name_conflict(const FSParser::ClassNode *p_class_node, const StringName &p_member_name, const FSParser::Node *p_member_node) {
	const FSParser::DataType *current_data_type = &p_class_node->base_type;
	while (current_data_type && current_data_type->kind == FSParser::DataType::Kind::CLASS) {
		FSParser::ClassNode *current_class_node = current_data_type->class_type;
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
	if (current_data_type && current_data_type->kind == FSParser::DataType::Kind::NATIVE) {
		if (current_data_type->native_type != StringName()) {
			const Error err = check_native_member_name_conflict(
					p_member_name,
					p_member_node,
					current_data_type->native_type);
			if (err != OK) {
				return err;
			}
		}
	}

	return check_outer_class_member_name_conflict(p_class_node, p_member_name, p_member_node);
}

void FSAnalyzer::get_class_node_current_scope_classes(FSParser::ClassNode *p_node, List<FSParser::ClassNode *> *p_list, FSParser::Node *p_source) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(p_list);

	if (p_list->find(p_node) != nullptr) {
		return;
	}

	p_list->push_back(p_node);

	auto resolve_for_scope_traverse = [&](FSParser::ClassNode *p_scope_class) {
		if (p_scope_class == nullptr || p_scope_class->base_type.is_resolving()) {
			return;
		}
		resolve_class_inheritance(p_scope_class, p_source);
	};

	// Prioritize node base type over its outer class.
	if (p_node->base_type.class_type != nullptr) {
		resolve_for_scope_traverse(p_node->base_type.class_type);
		get_class_node_current_scope_classes(p_node->base_type.class_type, p_list, p_source);
	}

	if (p_node->outer != nullptr) {
		resolve_for_scope_traverse(p_node->outer);
		get_class_node_current_scope_classes(p_node->outer, p_list, p_source);
	}
}

Error FSAnalyzer::resolve_class_inheritance(FSParser::ClassNode *p_class, const FSParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<FSParserRef> parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class inheritance", p_source);
	Finally finally([&]() {
		for (FSParser::ClassNode *look_class = p_class; look_class != nullptr; look_class = look_class->base_type.class_type) {
			dependency_parser_access.ensure_cached_external_parser_for_class(look_class->base_type.class_type, look_class, "Trying to resolve class inheritance", p_source);
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

		Error err = dependency_parser_access.raise_parser_to_status(parser_ref, FSParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
			return ERR_PARSE_ERROR;
		}

		FSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		FSParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_inheritance(p_class);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve inheritance for class "%s".)", p_class->fqcn), p_source);
			return ERR_PARSE_ERROR;
		}

		return OK;
	}

	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	if (p_class->identifier) {
		StringName class_name = p_class->identifier->name;
		StringName global_class_name = (p_class == parser->head && !p_class->qualified_global_name.is_empty()) ? StringName(p_class->qualified_global_name) : class_name;
		bool declares_script_owned_autoload = false;
		if (p_class == parser->head) {
			for (FSParser::AnnotationNode *annotation : parser->head->annotations) {
				if (annotation->name == SNAME("@autoload")) {
					declares_script_owned_autoload = true;
					break;
				}
			}
		}
		ensure_autoload_index_current();
		if (FSParser::get_builtin_type(class_name) < Variant::VARIANT_MAX || class_name == SNAME("AsyncCallable")) {
			push_error(vformat(R"(Class "%s" hides a built-in type.)", class_name), p_class->identifier);
		} else if (class_exists(class_name)) {
			push_error(vformat(R"(Class "%s" hides a native class.)", class_name), p_class->identifier);
		} else if (ScriptServer::is_global_class(global_class_name) && (!FoundryScript::is_canonically_equal_paths(ScriptServer::get_global_class_path(global_class_name), parser->script_path) || p_class != parser->head)) {
			push_error(vformat(R"(Class "%s" from "%s" collides with global script class from "%s".)", global_class_name, parser->script_path, ScriptServer::get_global_class_path(global_class_name)), p_class->identifier);
		} else if (const FSAutoloadIndexEntry *autoload = autoload_index.get_by_name(class_name); autoload != nullptr && autoload->is_singleton &&
				!declares_script_owned_autoload &&
				(p_class != parser->head || !FoundryScript::is_canonically_equal_paths(autoload->path, parser->script_path))) {
			push_error(vformat(R"(Class "%s" hides an autoload singleton.)", class_name), p_class->identifier);
		}
	}

	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;
	p_class->base_type = resolving_datatype;

	// Set datatype for class.
	FSParser::DataType class_type;
	class_type.is_constant = true;
	class_type.is_meta_type = true;
	class_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	class_type.kind = FSParser::DataType::CLASS;
	class_type.class_type = p_class;
	class_type.script_path = parser->script_path;
	class_type.builtin_type = Variant::OBJECT;
	p_class->set_datatype(class_type);

	FSParser::DataType result;
	// Tracks which extends type arguments failed to resolve, so the deferred bound check below skips
	// them and does not emit a second diagnostic for an already-reported argument.
	Vector<bool> extends_argument_failed;
	if (!p_class->extends_used) {
		result.type_source = FSParser::DataType::ANNOTATED_INFERRED;
		result.kind = FSParser::DataType::NATIVE;
		result.builtin_type = Variant::OBJECT;
		result.native_type = SNAME("RefCounted");
	} else {
		result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

		FSParser::DataType base;

		int extends_index = 0;

		if (!p_class->extends_path.is_empty()) {
			if (p_class->extends_path.is_relative_path()) {
				p_class->extends_path = class_type.script_path.get_base_dir().path_join(p_class->extends_path).simplify_path();
			}
			Ref<FSParserRef> ext_parser;
			Error err = dependency_parser_access.raise_depended_parser_for(p_class->extends_path, FSParserRef::INHERITANCE_SOLVED, ext_parser);
			if (ext_parser.is_null()) {
				push_error(vformat(R"(Could not resolve super class path "%s".)", p_class->extends_path), p_class);
				return ERR_PARSE_ERROR;
			}

			if (err != OK) {
				push_error(vformat(R"(Could not resolve super class inheritance from "%s".)", p_class->extends_path), p_class);
				return err;
			}

#ifdef DEBUG_ENABLED
			if (!parser->_is_tool && ext_parser->get_parser()->_is_tool) {
				parser->push_warning(p_class, FSWarning::MISSING_TOOL);
			}
#endif // DEBUG_ENABLED

			base = ext_parser->get_parser()->head->get_datatype();
		} else {
			if (p_class->extends.is_empty()) {
				push_error("Could not resolve an empty super class path.", p_class);
				return ERR_PARSE_ERROR;
			}
			FSParser::IdentifierNode *id = p_class->extends[extends_index++];
			const StringName &name = id->name;
			base.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;

			if (ScriptServer::is_global_class(name)) {
				if (reject_bootstrap_global_class_dependency(name, id, "global superclass")) {
					return ERR_PARSE_ERROR;
				}

				String base_path = ScriptServer::get_global_class_path(name);

				if (FoundryScript::is_canonically_equal_paths(base_path, parser->script_path)) {
					base = parser->head->get_datatype();
				} else {
					Ref<FSParserRef> base_parser;
					Error err = dependency_parser_access.raise_depended_parser_for(base_path, FSParserRef::INHERITANCE_SOLVED, base_parser);
					if (base_parser.is_null()) {
						push_error(vformat(R"(Could not resolve super class "%s".)", name), id);
						return ERR_PARSE_ERROR;
					}

					if (err != OK) {
						push_error(vformat(R"(Could not resolve super class inheritance from "%s".)", name), id);
						return err;
					}

#ifdef DEBUG_ENABLED
					if (!parser->_is_tool && base_parser->get_parser()->_is_tool) {
						parser->push_warning(p_class, FSWarning::MISSING_TOOL);
					}
#endif // DEBUG_ENABLED

					base = base_parser->get_parser()->head->get_datatype();
				}
			} else if (class_exists(name)) {
				if (Engine::get_singleton()->has_singleton(name)) {
					push_error(vformat(R"(Cannot inherit native class "%s" because it is an engine singleton.)", name), id);
					return ERR_PARSE_ERROR;
				}
				base.kind = FSParser::DataType::NATIVE;
				base.builtin_type = Variant::OBJECT;
				base.native_type = name;
			} else {
				// Look for other classes in script.
				bool found = false;
				List<FSParser::ClassNode *> script_classes;
				get_class_node_current_scope_classes(p_class, &script_classes, id);
				for (FSParser::ClassNode *look_class : script_classes) {
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
						FSParser::ClassNode::Member member = look_class->get_member(name);
						FSParser::DataType member_datatype = member.get_datatype();

						switch (member.type) {
							case FSParser::ClassNode::Member::CLASS:
								break; // OK.
							case FSParser::ClassNode::Member::CONSTANT:
								if (member_datatype.kind != FSParser::DataType::SCRIPT && member_datatype.kind != FSParser::DataType::CLASS) {
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
			FSParser::IdentifierNode *id = p_class->extends[index];

			if (base.kind != FSParser::DataType::CLASS) {
				push_error(vformat(R"(Cannot get nested types for extension from non-FoundryScript type "%s".)", base.to_string()), id);
				return ERR_PARSE_ERROR;
			}

			reduce_identifier_from_base(id, &base);
			FSParser::DataType id_type = id->get_datatype();

			if (!id_type.is_set()) {
				push_error(vformat(R"(Could not find nested type "%s".)", id->name), id);
				return ERR_PARSE_ERROR;
			} else if (id_type.kind != FSParser::DataType::SCRIPT && id_type.kind != FSParser::DataType::CLASS) {
				push_error(vformat(R"(Identifier "%s" is not a preloaded script or class.)", id->name), id);
				return ERR_PARSE_ERROR;
			}

			base = id_type;
		}

		result = base;

		// Specialize a generic base, e.g. `Stack[T] extends List[T]` or `extends List[int]`. The
		// arguments are resolved in this class's scope so a child parameter like `T` binds here.
		if (!p_class->extends_type_arguments.is_empty()) {
			if (result.kind != FSParser::DataType::CLASS || result.class_type == nullptr) {
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

	if (result.kind == FSParser::DataType::CLASS && result.class_type != nullptr && result.class_type->is_trait) {
		const String class_name = _class_or_trait_name(p_class);
		const String trait_name = _class_or_trait_name(result.class_type);
		const FSParser::Node *source = p_class->extends.is_empty() ? static_cast<const FSParser::Node *>(p_class) : p_class->extends[0];
		push_error(vformat(R"(Class "%s" cannot extend trait "%s"; use "uses %s" instead.)", class_name, trait_name, trait_name), source);
		return ERR_PARSE_ERROR;
	}

	// A final class cannot be extended. Covers every extend form: a named/inner base
	// (CLASS) and a cross-file compiled base (SCRIPT).
	bool base_is_final = false;
	if (result.kind == FSParser::DataType::CLASS && result.class_type != nullptr) {
		base_is_final = result.class_type->is_final;
	} else if (result.kind == FSParser::DataType::SCRIPT) {
		Ref<FoundryScript> base_script = result.script_type;
		base_is_final = base_script.is_valid() && base_script->is_final();
	}
	if (base_is_final) {
		const FSParser::Node *source = p_class->extends.is_empty() ? static_cast<const FSParser::Node *>(p_class) : p_class->extends[0];
		push_error(vformat(R"(Cannot extend final class "%s".)", result.to_string()), source);
		return ERR_PARSE_ERROR;
	}

	// Check for cyclic inheritance.
	const FSParser::ClassNode *base_class = result.class_type;
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
		Vector<const FSParser::Node *> argument_sources;
		for (FSParser::TypeNode *argument_node : p_class->extends_type_arguments) {
			argument_sources.push_back(argument_node);
		}
		if (!check_class_type_argument_bounds(p_class->base_type, extends_argument_failed, argument_sources)) {
			return ERR_PARSE_ERROR;
		}
	}

	// Apply annotations.
	for (FSParser::AnnotationNode *&E : p_class->annotations) {
		resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_CLASS);
		E->apply(parser, p_class, p_class->outer);
	}

	parser->current_class = previous_class;

	return OK;
}

Error FSAnalyzer::resolve_class_inheritance(FSParser::ClassNode *p_class, bool p_recursive) {
	Error err = resolve_class_inheritance(p_class);
	if (err) {
		return err;
	}

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			if (p_class->members[i].type == FSParser::ClassNode::Member::CLASS) {
				err = resolve_class_inheritance(p_class->members[i].m_class, true);
				if (err) {
					return err;
				}
			}
		}
	}

	return OK;
}

void FSAnalyzer::resolve_function_signature_in_class(FSParser::FunctionNode *p_function,
		FSParser::ClassNode *p_class, const FSParser::Node *p_source) {
	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;
	resolve_function_signature(p_function, p_source);
	parser->current_class = previous_class;
}

FSParser::DataType FSAnalyzer::resolve_enum_values(FSParser::EnumNode *p_enum,
		const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner) {
	ERR_FAIL_NULL_V(p_enum, p_enum_type);
	ERR_FAIL_NULL_V(p_owner, p_enum_type);

	if (p_enum->get_datatype().is_set()) {
		return p_enum->get_datatype();
	}

	AnalysisScopeGuard scope(this, p_owner);
	current_enum = p_enum;

	FSParser::DataType enum_type = p_enum_type;
	enum_type.is_tagged_union = p_enum->is_tagged_union;
	Dictionary dictionary;
	for (int i = 0; i < p_enum->values.size(); i++) {
		FSParser::EnumNode::Value &element = p_enum->values.write[i];

		if (enum_type.is_tagged_union) {
			// Tags are ordinal by declaration order; explicit values are already rejected by the parser.
			element.value = i;
			element.resolved = true;

			if (element.has_payload()) {
				FSParser::DataType::EnumCasePayload payload;
				for (const FSParser::EnumNode::PayloadField &field : element.payload_fields) {
					payload.field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
					payload.field_types.push_back(type_from_metatype(resolve_datatype(field.type)));
				}
				enum_type.enum_case_payloads[element.identifier->name] = payload;
			}
		} else if (element.custom_value) {
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
			push_error(R"(Enum values must have an explicit integer value.)", element.identifier);
		}

		enum_type.enum_values[element.identifier->name] = element.value;
		dictionary[String(element.identifier->name)] = element.value;

#ifdef DEBUG_ENABLED
		// Named enum identifiers do not shadow anything since they are qualified at ordinary use sites.
		if (p_enum->identifier == nullptr || p_enum->identifier->name == StringName()) {
			is_shadowing(element.identifier, "enum member", false);
		}
#endif // DEBUG_ENABLED
	}

	dictionary.make_read_only();
	p_enum->set_datatype(enum_type);
	p_enum->dictionary = dictionary;
	return enum_type;
}

void FSAnalyzer::resolve_enum_interface(FSParser::EnumNode *p_enum,
		const FSParser::DataType &p_enum_type, FSParser::ClassNode *p_owner) {
	ERR_FAIL_NULL(p_enum);
	ERR_FAIL_NULL(p_owner);

	const FSParser::DataType enum_type = resolve_enum_values(p_enum, p_enum_type, p_owner);
	AnalysisScopeGuard scope(this, p_owner);
	current_enum = p_enum;

	HashSet<StringName> function_names;
	for (FSParser::FunctionNode *function : p_enum->functions) {
		if (function == nullptr || function->identifier == nullptr) {
			continue;
		}

		const StringName function_name = function->identifier->name;
		if (enum_type.enum_values.has(function_name)) {
			push_error(vformat(R"*(Enum function "%s()" conflicts with enum value "%s".)*",
							   function_name, function_name),
					function->identifier);
		}
		if (function_names.has(function_name)) {
			push_error(vformat(R"*(Enum function "%s()" is declared more than once.)*", function_name),
					function->identifier);
		} else {
			function_names.insert(function_name);
		}
		if (function->is_static && Variant::has_builtin_method(Variant::DICTIONARY, function_name)) {
			push_error(vformat(R"*(Static enum function "%s" conflicts with Dictionary method "%s()".)*",
							   function_name, function_name),
					function->identifier);
		}

		for (FSParser::AnnotationNode *&annotation : function->annotations) {
			resolve_annotation(annotation, FSParser::AnnotationDeclarationNode::TARGET_METHOD);
			annotation->apply(parser, function, p_owner);
		}
		resolve_function_signature(function);
	}
}

bool FSAnalyzer::resolve_type_parameter(const StringName &p_name, FSParser::DataType &r_type) {
	const FSParser::TypeParameterNode *parameter = nullptr;
	FSParser::DataType::TypeParameterScope scope = FSParser::DataType::TYPE_PARAMETER_NONE;
	int index = -1;

	auto match_in = [&](const Vector<FSParser::TypeParameterNode *> &p_parameters, FSParser::DataType::TypeParameterScope p_scope) -> bool {
		for (int i = 0; i < p_parameters.size(); i++) {
			const FSParser::TypeParameterNode *candidate = p_parameters[i];
			if (candidate != nullptr && candidate->identifier != nullptr && candidate->identifier->name == p_name) {
				parameter = candidate;
				scope = p_scope;
				index = i;
				return true;
			}
		}
		return false;
	};

	FSParser::ClassNode *declaring_class = nullptr;

	// Method type parameters shadow class ones, and inner classes shadow their outer classes. A
	// lambda body is analyzed with `current_function` set to the lambda's own function, which has no
	// type parameters, so walk out through each enclosing lambda to its parent function to keep the
	// surrounding generic method's parameters visible.
	bool found_method_parameter = false;
	for (FSParser::FunctionNode *enclosing = parser->current_function; enclosing != nullptr;) {
		if (match_in(enclosing->type_parameters, FSParser::DataType::TYPE_PARAMETER_METHOD)) {
			found_method_parameter = true;
			break;
		}
		enclosing = enclosing->source_lambda != nullptr ? enclosing->source_lambda->parent_function : nullptr;
	}
	if (found_method_parameter) {
		// Found a method type parameter.
	} else {
		for (FSParser::ClassNode *script_class = parser->current_class; script_class != nullptr; script_class = script_class->outer) {
			if (match_in(script_class->type_parameters, FSParser::DataType::TYPE_PARAMETER_CLASS)) {
				declaring_class = script_class;
				break;
			}
		}
	}

	if (parameter == nullptr) {
		return false;
	}

	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_name = p_name;
	type.type_parameter_scope = scope;
	type.type_parameter_index = index;
	if (parameter->bound != nullptr) {
		// A class type parameter's bound belongs to its declaring class, not wherever the parameter is
		// used. Resolve (and thus cache) it in that scope so an enclosing method type parameter cannot
		// shadow the bound name and poison the cached datatype for later uses.
		FSParser::ClassNode *previous_class = parser->current_class;
		FSParser::FunctionNode *previous_function = parser->current_function;
		if (scope == FSParser::DataType::TYPE_PARAMETER_CLASS && declaring_class != nullptr) {
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

FSParser::FunctionNode *FSAnalyzer::find_generic_method(FSParser::ClassNode *p_class, const StringName &p_name, bool &r_found_member) {
	// Find a generic method named `p_name` reachable from `p_class`, matching get_function_signature's
	// resolution order: the entire class/base chain is searched for an own member first, and only if
	// none is found are the starting class's applied traits consulted (base-class traits are already
	// flattened into the base members walked above). `r_found_member` reports whether any member of
	// that name exists, so callers can tell a missing method from a non-generic one.
	r_found_member = false;
	for (FSParser::ClassNode *lookup_class = p_class; lookup_class != nullptr; lookup_class = lookup_class->base_type.class_type) {
		if (lookup_class->has_member(p_name)) {
			r_found_member = true;
			const FSParser::ClassNode::Member &member = lookup_class->get_member(p_name);
			if (member.type == FSParser::ClassNode::Member::FUNCTION && member.function != nullptr && !member.function->type_parameters.is_empty()) {
				return member.function;
			}
			return nullptr;
		}
	}
	if (p_class != nullptr && (p_class->is_trait || !p_class->used_traits.is_empty())) {
		resolve_trait_uses(p_class);
		for (FSParser::ClassNode *trait : p_class->resolved_traits) {
			if (trait == nullptr || !trait->has_member(p_name)) {
				continue;
			}
			r_found_member = true;
			const FSParser::ClassNode::Member &member = trait->get_member(p_name);
			if (member.type == FSParser::ClassNode::Member::FUNCTION && member.function != nullptr && !member.function->type_parameters.is_empty()) {
				return member.function;
			}
			return nullptr;
		}
	}
	return nullptr;
}

bool FSAnalyzer::apply_class_type_arguments(FSParser::DataType &r_type, const Vector<FSParser::TypeNode *> &p_argument_nodes, const FSParser::Node *p_source, bool p_check_bounds, Vector<bool> *r_argument_failed) {
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
	Vector<FSParser::DataType> resolved_arguments;
	Vector<bool> argument_failed;
	Vector<const FSParser::Node *> argument_sources;
	bool nested_type_handle_argument = false;
	for (int i = 0; i < p_argument_nodes.size(); i++) {
		const int errors_before = parser->get_errors().size();
		FSParser::DataType argument = type_from_metatype(resolve_datatype(p_argument_nodes[i]));
		if (argument.is_type_handle_annotation) {
			push_error("Type[T] cannot be used as a nested type argument yet.", p_argument_nodes[i]);
			nested_type_handle_argument = true;
		}
		resolved_arguments.push_back(argument);
		argument_failed.push_back(parser->get_errors().size() > errors_before || argument.is_type_handle_annotation);
		argument_sources.push_back(p_argument_nodes[i]);
	}

	if (nested_type_handle_argument) {
		return false;
	}

	if (r_argument_failed != nullptr) {
		*r_argument_failed = argument_failed;
	}

	return bind_class_type_arguments(r_type, resolved_arguments, argument_failed, argument_sources, p_source, p_check_bounds);
}

bool FSAnalyzer::bind_class_type_arguments(FSParser::DataType &r_type, const Vector<FSParser::DataType> &p_arguments, const Vector<bool> &p_argument_failed, const Vector<const FSParser::Node *> &p_argument_sources, const FSParser::Node *p_source, bool p_check_bounds) {
	r_type.type_arguments = p_arguments;
	if (!p_check_bounds) {
		// The caller will validate the bounds later (e.g. after a class's specialized base is fully
		// installed, so a self-referential argument is checked against the real chain).
		return true;
	}
	return check_class_type_argument_bounds(r_type, p_argument_failed, p_argument_sources);
}

bool FSAnalyzer::check_class_type_argument_bounds(FSParser::DataType &r_type, const Vector<bool> &p_argument_failed, const Vector<const FSParser::Node *> &p_argument_sources) {
	const Vector<FSParser::TypeParameterNode *> &type_parameters = r_type.class_type->type_parameters;

	// Bind every parameter to its argument so a dependent bound like `[U: Resource, T: U]` (or its
	// forward-referencing form `[T: U, U: Resource]`) is checked against the concrete argument
	// supplied for the referenced sibling.
	HashMap<StringName, FSParser::DataType> bindings;
	for (int i = 0; i < r_type.type_arguments.size(); i++) {
		const FSParser::TypeParameterNode *parameter = type_parameters[i];
		if (parameter != nullptr && parameter->identifier != nullptr) {
			bindings.insert(parameter->identifier->name, r_type.type_arguments[i]);
		}
	}

	bool bound_violation = false;
	for (int i = 0; i < r_type.type_arguments.size(); i++) {
		const FSParser::TypeParameterNode *parameter = type_parameters[i];
		// An argument that failed to resolve already reported an error; skip it to avoid a second
		// diagnostic (a failed resolve yields a `Variant` placeholder that would otherwise be checked
		// against the bound and re-rejected).
		if (parameter == nullptr || parameter->bound == nullptr || p_argument_failed[i]) {
			continue;
		}
		// Resolve the bound in the generic class's own scope so relative bound names bind to the
		// declaring class rather than the (possibly unrelated) use site, where an enclosing
		// class or method type parameter could otherwise shadow them.
		FSParser::ClassNode *previous_class = parser->current_class;
		FSParser::FunctionNode *previous_function = parser->current_function;
		parser->current_class = r_type.class_type;
		parser->current_function = nullptr;
		const FSParser::DataType bound = type_from_metatype(resolve_datatype(parameter->bound));
		parser->current_class = previous_class;
		parser->current_function = previous_function;

		// An unresolved or unconstrained (`Variant`) bound imposes no requirement.
		if (!bound.is_set() || bound.is_variant()) {
			continue;
		}
		const FSParser::DataType effective_bound = bindings.is_empty() ? bound : FSParser::DataType::substitute(bound, bindings);
		if (!type_argument_satisfies_bound(r_type.type_arguments[i], effective_bound)) {
			push_error(vformat(R"(Type argument "%s" does not satisfy the bound "%s" of type parameter "%s".)", r_type.type_arguments[i].to_string(), effective_bound.to_string(), parameter->identifier->name), p_argument_sources[i]);
			bound_violation = true;
		}
	}
	return !bound_violation;
}

FSParser::DataType FSAnalyzer::specialize_ancestor_type(const FSParser::DataType &p_base, const FSParser::ClassNode *p_target) {
	// Walks the inheritance chain from a specialized base down to `p_target`, applying each level's
	// type arguments so a member declared in an ancestor sees the concrete arguments supplied at the
	// most-derived use site. For `Stack[int] extends List[U]`, reaching `List` yields `List[int]`.
	FSParser::DataType current = p_base;
	while (current.class_type != nullptr) {
		if (current.class_type == p_target) {
			return current;
		}

		FSParser::DataType parent = current.class_type->base_type;
		if (parent.class_type == nullptr) {
			break;
		}

		// Rewrite the parent handle's type arguments (which reference `current`'s parameters) into
		// the concrete arguments bound at this level.
		if (current.has_type_arguments()) {
			const Vector<FSParser::TypeParameterNode *> &type_parameters = current.class_type->type_parameters;
			HashMap<StringName, FSParser::DataType> bindings;
			const int binding_count = MIN(type_parameters.size(), current.type_arguments.size());
			for (int i = 0; i < binding_count; i++) {
				const FSParser::TypeParameterNode *parameter = type_parameters[i];
				if (parameter != nullptr && parameter->identifier != nullptr) {
					bindings.insert(parameter->identifier->name, current.type_arguments[i]);
				}
			}
			if (!bindings.is_empty()) {
				parent = FSParser::DataType::substitute(parent, bindings);
			}
		}

		current = parent;
	}

	// `p_target` is not on the inheritance chain (e.g. an outer class or applied trait); return a
	// non-specialized handle so member substitution is a no-op.
	FSParser::DataType fallback = p_base;
	fallback.type_arguments.clear();
	return fallback;
}

void FSAnalyzer::resolve_class_member(FSParser::ClassNode *p_class, const StringName &p_name, const FSParser::Node *p_source) {
	ERR_FAIL_COND(!p_class->has_member(p_name));
	resolve_class_member(p_class, p_class->members_indices[p_name], p_source);
}

void FSAnalyzer::resolve_class_member(FSParser::ClassNode *p_class, int p_index, const FSParser::Node *p_source) {
	ERR_FAIL_INDEX(p_index, p_class->members.size());

	FSParser::ClassNode::Member &member = p_class->members.write[p_index];
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = member.get_source_node();
	}

	Ref<FSParserRef> parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class member", p_source);
	Finally finally([&]() {
		dependency_parser_access.ensure_cached_external_parser_for_class(member.get_datatype().class_type, p_class, "Trying to resolve datatype of class member", p_source);
		FSParser::DataType member_type = member.get_datatype();
		for (int i = 0; i < member_type.get_container_element_type_count(); ++i) {
			dependency_parser_access.ensure_cached_external_parser_for_class(member_type.get_container_element_type(i).class_type, p_class, "Trying to resolve datatype of class member", p_source);
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

		Error err = dependency_parser_access.raise_parser_to_status(parser_ref, FSParserRef::PARSED);
		if (err) {
			push_error(vformat(R"(Could not parse script "%s": %s (While resolving external class member "%s").)", p_class->get_datatype().script_path, error_names[err], member.get_name()), p_source);
			return;
		}

		FSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		FSParser *other_parser = parser_ref->get_parser();

		int error_count = other_parser->errors.size();
		other_analyzer->resolve_class_member(p_class, p_index);
		if (other_parser->errors.size() > error_count) {
			push_error(vformat(R"(Could not resolve external class member "%s".)", member.get_name()), p_source);
			return;
		}

		return;
	}

	FSParser::ClassNode *previous_class = parser->current_class;
	parser->current_class = p_class;

	FSParser::DataType resolving_datatype;
	resolving_datatype.kind = FSParser::DataType::RESOLVING;

	{
#ifdef DEBUG_ENABLED
		FSParser::Node *member_node = member.get_source_node();
		if (member_node && member_node->type != FSParser::Node::ANNOTATION) {
			// Apply @warning_ignore annotations before resolving member.
			for (FSParser::AnnotationNode *&E : member_node->annotations) {
				if (E->name == SNAME("@warning_ignore")) {
					resolve_annotation(E);
					E->apply(parser, member.variable, p_class);
				}
			}
		}
#endif // DEBUG_ENABLED
		switch (member.type) {
			case FSParser::ClassNode::Member::VARIABLE: {
				bool previous_static_context = static_context;
				static_context = member.variable->is_static;

				check_class_member_name_conflict(p_class, member.variable->identifier->name, member.variable);

				member.variable->set_datatype(resolving_datatype);
				resolve_variable(member.variable, false);
				resolve_pending_lambda_bodies();

				// Tuples and tagged unions have no inspector representation, so exporting either is
				// rejected before the export annotation runs (it would otherwise report the erased
				// Array type). This also catches them nested in an exported `Array`/`Dictionary`,
				// where they would otherwise reach the inspector as bogus int-enum or Array metadata.
				const FSParser::DataType member_variable_datatype = member.variable->get_datatype();
				FSParser::DataType export_check_datatype = member_variable_datatype;
				if (export_check_datatype.is_variant() && member.variable->initializer != nullptr && member.variable->initializer->get_datatype().is_set()) {
					// `@export` itself infers the exported type from the initializer when the
					// declared type is `Variant` (see `FSParser::export_annotations`); mirror that
					// here so a `Variant`-declared property initialized to a tuple or tagged-union
					// value is still caught instead of reaching the enum export path unchecked.
					export_check_datatype = member.variable->initializer->get_datatype();
				}
				FSParser::DataType rejected_export_datatype;
				const bool rejects_export = _export_type_contains_tuple_or_tagged_union(export_check_datatype, rejected_export_datatype);

				// Apply annotations.
				for (FSParser::AnnotationNode *&E : member.variable->annotations) {
					if (E->name != SNAME("@warning_ignore")) {
						// `@export_storage` is explicitly type-independent (it persists the raw
						// value without going through the inspector's PropertyInfo mapping), so it
						// never hits the erased-type problem the other `@export*` annotations do.
						const bool is_inspector_export_annotation = String(E->name).begins_with("@export") && E->name != SNAME("@export_storage");
						if (rejects_export && is_inspector_export_annotation) {
							if (rejected_export_datatype.is_tuple()) {
								push_error(vformat(R"(Cannot export a tuple-typed property: "%s" has type "%s".)",
												   member.variable->identifier->name, export_check_datatype.to_string()),
										E);
							} else {
								push_error(vformat(R"(Cannot export a tagged-union-typed property: "%s" has type "%s".)",
												   member.variable->identifier->name, export_check_datatype.to_string()),
										E);
							}
							continue;
						}
						resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_VARIABLE);
						E->apply(parser, member.variable, p_class);
					}
				}

				static_context = previous_static_context;

#ifdef DEBUG_ENABLED
				if (member.variable->exported && member.variable->onready) {
					parser->push_warning(member.variable, FSWarning::ONREADY_WITH_EXPORT);
				}
				if (member.variable->initializer) {
					// Check if it is call to get_node() on self (using shorthand $ or not), so we can check if @onready is needed.
					// This could be improved by traversing the expression fully and checking the presence of get_node at any level.
					if (!member.variable->is_static && !member.variable->onready && member.variable->initializer && (member.variable->initializer->type == FSParser::Node::GET_NODE || member.variable->initializer->type == FSParser::Node::CALL || member.variable->initializer->type == FSParser::Node::CAST)) {
						FSParser::Node *expr = member.variable->initializer;
						if (expr->type == FSParser::Node::CAST) {
							expr = static_cast<FSParser::CastNode *>(expr)->operand;
						}
						bool is_get_node = expr->type == FSParser::Node::GET_NODE;
						bool is_using_shorthand = is_get_node;
						if (!is_get_node && expr->type == FSParser::Node::CALL) {
							is_using_shorthand = false;
							FSParser::CallNode *call = static_cast<FSParser::CallNode *>(expr);
							if (call->function_name == SNAME("get_node")) {
								switch (call->get_callee_type()) {
									case FSParser::Node::IDENTIFIER: {
										is_get_node = true;
									} break;
									case FSParser::Node::SUBSCRIPT: {
										FSParser::SubscriptNode *subscript = static_cast<FSParser::SubscriptNode *>(call->callee);
										is_get_node = subscript->is_attribute && subscript->base->type == FSParser::Node::SELF;
									} break;
									default:
										break;
								}
							}
						}
						if (is_get_node) {
							String offending_syntax = "get_node()";
							if (is_using_shorthand) {
								FSParser::GetNodeNode *get_node_node = static_cast<FSParser::GetNodeNode *>(expr);
								offending_syntax = get_node_node->use_dollar ? "$" : "%";
							}
							parser->push_warning(member.variable, FSWarning::GET_NODE_DEFAULT_WITHOUT_ONREADY, offending_syntax);
						}
					}
				}
#endif // DEBUG_ENABLED
			} break;
			case FSParser::ClassNode::Member::CONSTANT: {
				check_class_member_name_conflict(p_class, member.constant->identifier->name, member.constant);
				member.constant->set_datatype(resolving_datatype);
				resolve_constant(member.constant, false);

				// Apply annotations.
				for (FSParser::AnnotationNode *&E : member.constant->annotations) {
					resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_CONSTANT);
					E->apply(parser, member.constant, p_class);
				}
			} break;
			case FSParser::ClassNode::Member::SIGNAL: {
				check_class_member_name_conflict(p_class, member.signal->identifier->name, member.signal);

				member.signal->set_datatype(resolving_datatype);

				// This is the _only_ way to declare a signal. Therefore, we can generate its
				// MethodInfo inline so it's a tiny bit more efficient.
				MethodInfo mi = MethodInfo(member.signal->identifier->name);

				for (int j = 0; j < member.signal->parameters.size(); j++) {
					FSParser::ParameterNode *param = member.signal->parameters[j];
					resolve_parameter(param);
					FSParser::DataType param_type = type_from_metatype(resolve_datatype(param->datatype_specifier));
					param->set_datatype(param_type);
#ifdef DEBUG_ENABLED
					if (param->datatype_specifier == nullptr) {
						parser->push_warning(param, FSWarning::UNTYPED_DECLARATION, "Parameter", param->identifier->name);
					}
#endif // DEBUG_ENABLED
					mi.arguments.push_back(param_type.to_property_info(param->identifier->name));
					// Signals do not support parameter default values.
				}
				member.signal->set_datatype(make_signal_type(mi, member.signal));
				member.signal->method_info = mi;

				// Apply annotations.
				for (FSParser::AnnotationNode *&E : member.signal->annotations) {
					resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_SIGNAL);
					E->apply(parser, member.signal, p_class);
				}
			} break;
			case FSParser::ClassNode::Member::ENUM: {
				check_class_member_name_conflict(p_class, member.m_enum->identifier->name, member.m_enum);

				member.m_enum->set_datatype(resolving_datatype);
				FSParser::DataType enum_type = make_class_enum_type(member.m_enum->identifier->name, p_class, parser->script_path, true);
				resolve_enum_interface(member.m_enum, enum_type, p_class);

				// Apply annotations.
				for (FSParser::AnnotationNode *&E : member.m_enum->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.m_enum, p_class);
				}
			} break;
			case FSParser::ClassNode::Member::FUNCTION:
				check_outer_class_member_name_conflict(p_class, member.function->identifier->name, member.function);
				for (FSParser::AnnotationNode *&E : member.function->annotations) {
					resolve_annotation(E, FSParser::AnnotationDeclarationNode::TARGET_METHOD);
					E->apply(parser, member.function, p_class);
				}
				resolve_function_signature(member.function, p_source);
				break;
			case FSParser::ClassNode::Member::ENUM_VALUE: {
				member.enum_value.identifier->set_datatype(resolving_datatype);

				if (member.enum_value.custom_value) {
					check_class_member_name_conflict(p_class, member.enum_value.identifier->name, member.enum_value.custom_value);

					const FSParser::EnumNode *prev_enum = current_enum;
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
					if (member.enum_value.parent_enum != nullptr && member.enum_value.parent_enum->is_tagged_union) {
						// A tagged union's cases are only reachable as `Enum.Case`, so an unnamed enum has
						// no way to spell them.
						push_error(R"(Payload-carrying cases require a named enum; an unnamed enum cannot qualify its cases.)",
								member.enum_value.identifier);
					} else {
						push_error(R"(Enum values must have an explicit integer value.)", member.enum_value.identifier);
					}
				}

				// Also update the original references.
				member.enum_value.parent_enum->values.set(member.enum_value.index, member.enum_value);

				member.enum_value.identifier->set_datatype(make_class_enum_type(UNNAMED_ENUM, p_class, parser->script_path, false));
			} break;
			case FSParser::ClassNode::Member::CLASS:
				check_class_member_name_conflict(p_class, member.m_class->identifier->name, member.m_class);
				// If it's already resolving, that's ok.
				if (!member.m_class->base_type.is_resolving()) {
					resolve_class_inheritance(member.m_class, p_source);
				}
				break;
			case FSParser::ClassNode::Member::GROUP:
				// No-op, but needed to silence warnings.
				break;
			case FSParser::ClassNode::Member::TUPLE: {
				check_class_member_name_conflict(p_class, member.m_tuple->identifier->name, member.m_tuple);

				// Marking the declaration as resolving turns a by-value self-containing tuple
				// (`tuple Node(value: int, child: Node)`) into a cycle error at the field that
				// closes the loop, instead of an infinite resolution.
				member.m_tuple->set_datatype(resolving_datatype);

				Vector<FSParser::DataType> element_types;
				Vector<StringName> field_names;
				for (int i = 0; i < member.m_tuple->fields.size(); i++) {
					const FSParser::TupleNode::Field &field = member.m_tuple->fields[i];
					element_types.push_back(type_from_metatype(resolve_datatype(field.type)));
					field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
				}
				member.m_tuple->set_datatype(make_tuple_type(member.m_tuple->identifier->name, p_class->fqcn,
						parser->script_path, element_types, field_names, true));

				// Apply annotations.
				for (FSParser::AnnotationNode *&E : member.m_tuple->annotations) {
					resolve_annotation(E);
					E->apply(parser, member.m_tuple, p_class);
				}
			} break;
			case FSParser::ClassNode::Member::UNDEFINED:
				ERR_PRINT("Trying to resolve undefined member.");
				break;
		}
	}

	parser->current_class = previous_class;
}

void FSAnalyzer::resolve_class_interface(FSParser::ClassNode *p_class, const FSParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<FSParserRef> parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(p_class, nullptr, "Trying to resolve class interface", p_source);

	if (!p_class->resolved_interface) {
#ifdef DEBUG_ENABLED
		bool has_static_data = p_class->has_static_data;
#endif // DEBUG_ENABLED

		if (!parser->has_class(p_class)) {
			if (parser_ref.is_null()) {
				// Error already pushed.
				return;
			}

			Error err = dependency_parser_access.raise_parser_to_status(parser_ref, FSParserRef::PARSED);
			if (err) {
				push_error(vformat(R"(Could not parse script "%s": %s.)", p_class->get_datatype().script_path, error_names[err]), p_source);
				return;
			}

			FSAnalyzer *other_analyzer = parser_ref->get_analyzer();
			FSParser *other_parser = parser_ref->get_parser();

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

		// `enum_name` declarations are stored outside the normal member list, but
		// compiling the enum-file itself still needs resolved values and dictionary.
		if (p_class == parser->head && p_class->is_enum_file && p_class->enum_file_decl != nullptr && p_class->enum_file_decl->identifier != nullptr) {
			const StringName global_enum_name = p_class->qualified_global_name.is_empty() ? p_class->enum_file_decl->identifier->name : StringName(p_class->qualified_global_name);
			const FSParser::DataType enum_type = make_global_enum_type_from_current_parser(global_enum_name, p_class);
			resolve_enum_interface(p_class->enum_file_decl, enum_type, p_class);
		}

		// Resolve declared type-parameter bounds eagerly so runtime reflection can report them even
		// when a parameter is never referenced inside the class body. A class parameter's bound is
		// resolved in its declaring class scope, mirroring `resolve_type_parameter`.
		if (!p_class->type_parameters.is_empty()) {
			FSParser::ClassNode *previous_class = parser->current_class;
			FSParser::FunctionNode *previous_function = parser->current_function;
			parser->current_class = p_class;
			parser->current_function = nullptr;
			for (FSParser::TypeParameterNode *parameter : p_class->type_parameters) {
				if (parameter != nullptr && parameter->bound != nullptr) {
					parameter->resolved_bound = type_from_metatype(resolve_datatype(parameter->bound));
				}
			}
			parser->current_class = previous_class;
			parser->current_function = previous_function;
		}

		FSParser::DataType base_type = p_class->base_type;
		if (base_type.kind == FSParser::DataType::CLASS) {
			FSParser::ClassNode *base_class = base_type.class_type;
			resolve_class_interface(base_class, p_class);
		}

		for (int i = 0; i < p_class->members.size(); i++) {
			resolve_class_member(p_class, i);

#ifdef DEBUG_ENABLED
			if (!has_static_data) {
				FSParser::ClassNode::Member member = p_class->members[i];
				if (member.type == FSParser::ClassNode::Member::CLASS) {
					has_static_data = member.m_class->has_static_data;
				}
			}
#endif // DEBUG_ENABLED
		}

#ifdef DEBUG_ENABLED
		if (!has_static_data && p_class->annotated_static_unload) {
			FSParser::Node *static_unload = nullptr;
			for (FSParser::AnnotationNode *node : p_class->annotations) {
				if (node->name == "@static_unload") {
					static_unload = node;
					break;
				}
			}
			parser->push_warning(static_unload ? static_unload : p_class, FSWarning::REDUNDANT_STATIC_UNLOAD);
		}
#endif // DEBUG_ENABLED
	}
}

void FSAnalyzer::resolve_class_interface(FSParser::ClassNode *p_class, bool p_recursive) {
	resolve_class_interface(p_class);

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			FSParser::ClassNode::Member member = p_class->members[i];
			if (member.type == FSParser::ClassNode::Member::CLASS) {
				resolve_class_interface(member.m_class, true);
			}
		}
	}
}

Error FSAnalyzer::resolve_trait_uses(FSParser::ClassNode *p_class, const FSParser::Node *p_source) {
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = p_class;
	}

	Ref<FSParserRef> parser_ref = dependency_parser_access.ensure_cached_external_parser_for_class(p_class, nullptr,
			"Trying to resolve trait uses", p_source);

	if (!parser->has_class(p_class)) {
		if (parser_ref.is_null()) {
			return ERR_PARSE_ERROR;
		}

		Error err = dependency_parser_access.raise_parser_to_status(parser_ref, FSParserRef::INTERFACE_SOLVED);
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
	HashMap<const FSParser::ClassNode *, HashMap<StringName, FSParser::DataType>> seen_trait_bindings;
	auto record_trait_binding = [&](const FSParser::ClassNode *p_seen_trait, const HashMap<StringName, FSParser::DataType> &p_binding, const FSParser::Node *p_binding_source) -> bool {
		if (p_binding.is_empty()) {
			return true;
		}
		const HashMap<StringName, FSParser::DataType> *previous = seen_trait_bindings.getptr(p_seen_trait);
		if (previous == nullptr) {
			seen_trait_bindings.insert(p_seen_trait, p_binding);
			return true;
		}
		for (const KeyValue<StringName, FSParser::DataType> &entry : p_binding) {
			const FSParser::DataType *previous_argument = previous->getptr(entry.key);
			if (previous_argument != nullptr && !_datatype_alpha_equal(entry.value, *previous_argument)) {
				push_error(vformat(R"(Trait "%s" is applied with conflicting type arguments ("%s" and "%s") through different traits used by "%s".)",
								   _class_or_trait_name(p_seen_trait), previous_argument->to_string(), entry.value.to_string(), _class_or_trait_name(p_class)),
						p_binding_source);
				return false;
			}
		}
		return true;
	};

	for (FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		const FSParser::Node *source = _trait_use_source(trait_use, p_class);
		FSParser::ClassNode *trait = resolve_trait_reference(p_class, trait_use, source);
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
			FSParser::DataType trait_handle = type_from_metatype(trait->get_datatype());
			trait_handle.is_meta_type = false;
			// Resolve the type arguments in `p_class`'s scope so a generic trait can forward its own
			// type parameter into a generic supertrait (`trait Wrapper[T] uses Storage[T]`).
			FSParser::ClassNode *previous_class = parser->current_class;
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
		HashMap<StringName, FSParser::DataType> direct_substitution;
		if (!trait->type_parameters.is_empty() && !trait_use.resolved_type_arguments.is_empty()) {
			const int count = MIN(trait->type_parameters.size(), trait_use.resolved_type_arguments.size());
			for (int i = 0; i < count; i++) {
				const FSParser::TypeParameterNode *type_parameter = trait->type_parameters[i];
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					direct_substitution.insert(type_parameter->identifier->name, trait_use.resolved_type_arguments[i]);
				}
			}
		}
		if (!record_trait_binding(trait, direct_substitution, source)) {
			return fail();
		}

		for (FSParser::ClassNode *transitive_trait : trait->resolved_traits) {
			if (!class_satisfies_trait_base(p_class, transitive_trait)) {
				push_error(vformat(R"(Class "%s" cannot use trait "%s" because it does not inherit from "%s".)",
								   _class_or_trait_name(p_class), _class_or_trait_name(transitive_trait),
								   transitive_trait->base_type.to_string()),
						source);
				return fail();
			}
			// Re-specialize how `trait` binds this supertrait into the class's frame, then check it
			// against any binding the supertrait already received through another path.
			HashMap<StringName, FSParser::DataType> transitive_binding;
			for (const KeyValue<StringName, FSParser::DataType> &entry : trait_type_argument_substitution(trait, transitive_trait)) {
				transitive_binding.insert(entry.key, FSParser::DataType::substitute(entry.value, direct_substitution));
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

Error FSAnalyzer::resolve_trait_uses(FSParser::ClassNode *p_class, bool p_recursive) {
	Error err = resolve_trait_uses(p_class);
	if (err != OK) {
		return err;
	}

	if (p_recursive) {
		for (int i = 0; i < p_class->members.size(); i++) {
			if (p_class->members[i].type == FSParser::ClassNode::Member::CLASS) {
				err = resolve_trait_uses(p_class->members[i].m_class, true);
				if (err != OK) {
					return err;
				}
			}
		}
	}

	return OK;
}
