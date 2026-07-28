/**************************************************************************/
/*  fs_docgen.cpp                                                         */
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

#include "fs_docgen.h"

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_autoload_index.h"

#include "core/variant/container_type_validate.h"

HashMap<String, String> FSDocGen::singletons;

String FSDocGen::_get_script_name(const String &p_path) {
	const HashMap<String, String>::ConstIterator E = singletons.find(p_path);
	if (E) {
		return E->value;
	}
	return p_path.trim_prefix("res://").quote();
}

String FSDocGen::_get_class_name(const GDP::ClassNode &p_class) {
	const GDP::ClassNode *curr_class = &p_class;
	if (!curr_class->identifier) { // All inner classes have an identifier, so this is the outer class.
		return _get_script_name(curr_class->fqcn);
	}

	String full_name = curr_class->identifier->name;
	while (curr_class->outer) {
		curr_class = curr_class->outer;
		if (!curr_class->identifier) { // All inner classes have an identifier, so this is the outer class.
			return vformat("%s.%s", _get_script_name(curr_class->fqcn), full_name);
		}
		full_name = vformat("%s.%s", curr_class->identifier->name, full_name);
	}
	return full_name;
}

void FSDocGen::_populate_singletons_from_autoload_index() {
	FSAutoloadIndex autoload_index;
	autoload_index.rebuild_from_project_settings();

	for (const FSAutoloadIndexEntry &autoload : autoload_index.get_entries()) {
		if (!autoload.is_singleton) {
			continue;
		}
		singletons[autoload.path] = autoload.name;
		if (!autoload.script_path.is_empty()) {
			singletons[autoload.script_path] = autoload.name;
		}
	}
}

static String _doccontainer_type_from_container_type(const ContainerType &p_type) {
	if (p_type.builtin_type == Variant::NIL) {
		return "Variant";
	}
	if (p_type.builtin_type == Variant::ARRAY && !p_type.element_types.is_empty()) {
		return vformat("Array[%s]", _doccontainer_type_from_container_type(p_type.element_types[0]));
	}
	if (p_type.builtin_type == Variant::DICTIONARY && !p_type.element_types.is_empty()) {
		const String key = p_type.element_types.size() > 0 ? _doccontainer_type_from_container_type(p_type.element_types[0]) : String("Variant");
		const String value = p_type.element_types.size() > 1 ? _doccontainer_type_from_container_type(p_type.element_types[1]) : String("Variant");
		return vformat("Dictionary[%s, %s]", key, value);
	}
	if (p_type.script.is_valid()) {
		if (p_type.script->get_global_name() != StringName()) {
			return p_type.script->get_global_name();
		}
		if (!p_type.script->get_path().get_file().is_empty()) {
			return p_type.script->get_path().get_file();
		}
	}
	if (p_type.class_name != StringName()) {
		return p_type.class_name;
	}
	return Variant::get_type_name(p_type.builtin_type);
}

// Qualifies the nominal identity of a script-declared type (enum or named tuple) for the class
// reference: `res://` owner paths become the script's documented name, so the rendered spelling is
// `Player.PlayerWorldPosition` rather than a raw resource path.
String FSDocGen::_qualified_declared_type_name(const StringName &p_native_type) {
	String qualified = String(p_native_type);
	if (qualified.begins_with("res://")) {
		// The owning script path is the leading segment: it ends at the first `::` nested-class
		// separator, or at the last `.` when the declaration sits directly in the root class.
		// Splitting on the last `.` unconditionally would fold an inner class name into the file
		// name and produce a link to a page that does not exist.
		int path_end = qualified.find("::");
		if (path_end < 0) {
			path_end = qualified.rfind_char('.');
		}
		if (path_end >= 0) {
			qualified = _get_script_name(qualified.left(path_end)) + qualified.substr(path_end);
		} else {
			qualified = _get_script_name(qualified);
		}
	}
	return qualified.replace("::", ".");
}

String FSDocGen::_structural_tuple_spelling(const GDType &p_gdtype) {
	String spelling = "(";
	for (int i = 0; i < p_gdtype.container_element_types.size(); i++) {
		if (i > 0) {
			spelling += ", ";
		}
		String element_type;
		String element_enum;
		_doctype_from_gdtype_nested(p_gdtype.container_element_types[i], element_type, element_enum);
		spelling += element_type.is_empty() ? String("Variant") : element_type;
	}
	return spelling + ")";
}

void FSDocGen::_doctype_from_gdtype_nested(const GDType &p_gdtype, String &r_type, String &r_enum) {
	String nested_tuple;
	_doctype_from_gdtype(p_gdtype, r_type, r_enum, nested_tuple);
	if (!nested_tuple.is_empty()) {
		// Only one tuple channel travels with a documented type, and it describes the outermost
		// type. A nested named tuple therefore has nowhere to carry its link target, so it is
		// spelled structurally: rendering the name without the channel would emit a dead
		// class-style help link, exactly as an enum nested in `Coroutine[T]` collapses to `int`.
		r_type = _structural_tuple_spelling(p_gdtype);
	}
}

void FSDocGen::_doctype_from_gdtype(const GDType &p_gdtype, String &r_type, String &r_enum, String &r_tuple, bool p_is_return) {
	if (!p_gdtype.is_hard_type()) {
		r_type = "Variant";
		return;
	}
	if (p_gdtype.is_coroutine) {
		// `Coroutine[T]` is a source-level skin over `FSFunctionState`. Render the
		// phantom result type from `container_element_types[0]` so the native class name
		// never leaks into the class reference, mirroring `DataType::to_string()`.
		String element = "Variant";
		if (p_gdtype.has_container_element_type(0)) {
			const GDType result_type = p_gdtype.get_container_element_type(0);
			if (result_type.kind == GDType::BUILTIN && result_type.builtin_type == Variant::NIL) {
				element = "void";
			} else {
				// An enum result collapses to its underlying `int` spelling, mirroring how
				// `Array[Enum]` renders as `int[]` in the class reference: the enum name lives
				// in a separate metadata field that the wrapped `Coroutine[T]` spelling cannot
				// carry, and embedding it here would emit a dead class-style help link.
				String element_enum;
				_doctype_from_gdtype_nested(result_type, element, element_enum);
				if (element.is_empty()) {
					element = "Variant";
				}
			}
		}
		r_type = "Coroutine[" + element + "]";
		return;
	}
	switch (p_gdtype.kind) {
		case GDType::BUILTIN:
			if (p_gdtype.builtin_type == Variant::NIL) {
				r_type = p_is_return ? "void" : "null";
				return;
			}
			if (p_gdtype.builtin_type == Variant::ARRAY && p_gdtype.has_container_element_type(0)) {
				_doctype_from_gdtype_nested(p_gdtype.get_container_element_type(0), r_type, r_enum);
				if (!r_enum.is_empty()) {
					r_type = "int[]";
					r_enum += "[]";
					return;
				}
				if (!r_type.is_empty() && r_type != "Variant") {
					r_type += "[]";
					return;
				}
			}
			if (p_gdtype.builtin_type == Variant::DICTIONARY && p_gdtype.has_container_element_types()) {
				String key, value;
				_doctype_from_gdtype_nested(p_gdtype.get_container_element_type_or_variant(0), key, r_enum);
				_doctype_from_gdtype_nested(p_gdtype.get_container_element_type_or_variant(1), value, r_enum);
				if (key != "Variant" || value != "Variant") {
					r_type = "Dictionary[" + key + ", " + value + "]";
					return;
				}
			}
			if (p_gdtype.builtin_type == Variant::CALLABLE && p_gdtype.signature_is_async) {
				r_type = "AsyncCallable";
				return;
			}
			r_type = Variant::get_type_name(p_gdtype.builtin_type);
			return;
		case GDType::NATIVE:
			if (p_gdtype.is_meta_type) {
				//r_type = FSNativeClass::get_class_static();
				r_type = "Object"; // "FSNativeClass" refers to a blank page.
				return;
			}
			r_type = p_gdtype.native_type;
			return;
		case GDType::SCRIPT:
			if (p_gdtype.is_meta_type) {
				r_type = p_gdtype.script_type.is_valid() ? p_gdtype.script_type->get_class_name() : Script::get_class_static();
				return;
			}
			if (p_gdtype.script_type.is_valid()) {
				if (p_gdtype.script_type->get_global_name() != StringName()) {
					r_type = p_gdtype.script_type->get_global_name();
					return;
				}
				if (!p_gdtype.script_type->get_path().is_empty()) {
					r_type = _get_script_name(p_gdtype.script_type->get_path());
					return;
				}
			}
			if (!p_gdtype.script_path.is_empty()) {
				r_type = _get_script_name(p_gdtype.script_path);
				return;
			}
			r_type = "Object";
			return;
		case GDType::CLASS:
			if (p_gdtype.is_meta_type) {
				r_type = FoundryScript::get_class_static();
				return;
			}
			if (p_gdtype.class_type->get_global_name() != StringName()) {
				r_type = p_gdtype.class_type->get_global_name();
				return;
			}
			r_type = _get_class_name(*p_gdtype.class_type);
			return;
		case GDType::ENUM:
			if (p_gdtype.is_meta_type) {
				r_type = "Dictionary";
				return;
			}
			r_type = "int";
			r_enum = _qualified_declared_type_name(p_gdtype.native_type);
			return;
		case GDType::TUPLE:
			if (p_gdtype.tuple_name != StringName()) {
				// A named tuple is nominal: document it by its own qualified name and point the
				// tuple channel at the declaration's entry.
				r_type = _qualified_declared_type_name(p_gdtype.native_type);
				if (String(p_gdtype.native_type) == String(p_gdtype.tuple_name)) {
					// A whole-file `tuple_name` declaration is its own documented class, so the
					// owning page is the qualified name itself and the entry inside that page is
					// keyed by the simple declaration name.
					const int slice_count = r_type.get_slice_count(".");
					r_tuple = r_type + "." + r_type.get_slicec('.', slice_count - 1);
				} else {
					r_tuple = r_type;
				}
				return;
			}
			// An unnamed tuple is structural: spell the parenthesized element list, recursing so
			// nested containers compose with the existing synthetic spellings.
			r_type = _structural_tuple_spelling(p_gdtype);
			return;
		case GDType::TYPE_PARAMETER:
			r_type = p_gdtype.type_parameter_name;
			return;
		case GDType::VARIANT:
		case GDType::RESOLVING:
		case GDType::UNRESOLVED:
			r_type = "Variant";
			return;
	}
}

String FSDocGen::_docvalue_from_variant(const Variant &p_variant, int p_recursion_level) {
	constexpr int MAX_RECURSION_LEVEL = 2;

	switch (p_variant.get_type()) {
		case Variant::STRING:
			return String(p_variant).c_escape().quote();
		case Variant::OBJECT:
			return "<Object>";
		case Variant::DICTIONARY: {
			const Dictionary dict = p_variant;
			String result;

			if (dict.is_typed()) {
				result += "Dictionary[";
				result += _doccontainer_type_from_container_type(dict.get_key_type());
				result += ", ";
				result += _doccontainer_type_from_container_type(dict.get_value_type());
				result += "](";
			}

			if (dict.is_empty()) {
				result += "{}";
			} else if (p_recursion_level > MAX_RECURSION_LEVEL) {
				result += "{...}";
			} else {
				result += "{";

				LocalVector<Variant> keys = dict.get_key_list();
				keys.sort_custom<StringLikeVariantOrder>();

				for (uint32_t i = 0; i < keys.size(); i++) {
					const Variant &key = keys[i];
					if (i > 0) {
						result += ", ";
					}
					result += _docvalue_from_variant(key, p_recursion_level + 1) + ": " + _docvalue_from_variant(dict[key], p_recursion_level + 1);
				}

				result += "}";
			}

			if (dict.is_typed()) {
				result += ")";
			}

			return result;
		} break;
		case Variant::ARRAY: {
			const Array array = p_variant;
			String result;

			if (array.is_typed()) {
				result += "Array[";
				result += _doccontainer_type_from_container_type(array.get_element_type());
				result += "](";
			}

			if (array.is_empty()) {
				result += "[]";
			} else if (p_recursion_level > MAX_RECURSION_LEVEL) {
				result += "[...]";
			} else {
				result += "[";

				for (int i = 0; i < array.size(); i++) {
					if (i > 0) {
						result += ", ";
					}
					result += _docvalue_from_variant(array[i], p_recursion_level + 1);
				}

				result += "]";
			}

			if (array.is_typed()) {
				result += ")";
			}

			return result;
		} break;
		default:
			return p_variant.get_construct_string();
	}
}

String FSDocGen::docvalue_from_enum_value(int64_t p_value, const HashMap<StringName, int64_t> &p_enum_values) {
	for (const KeyValue<StringName, int64_t> &E : p_enum_values) {
		if (E.value == p_value) {
			return E.key;
		}
	}
	return itos(p_value);
}

String FSDocGen::docvalue_from_expression(const GDP::ExpressionNode *p_expression, const GDType &p_type) {
	ERR_FAIL_NULL_V(p_expression, String());

	if (p_expression->is_constant) {
		if (p_type.kind == GDType::ENUM && !p_type.enum_values.is_empty() && p_expression->reduced_value.get_type() == Variant::INT) {
			return docvalue_from_enum_value(p_expression->reduced_value, p_type.enum_values);
		}
		if (p_expression->get_datatype().is_meta_type) {
			if (p_expression->type == GDP::Node::IDENTIFIER) {
				return static_cast<const GDP::IdentifierNode *>(p_expression)->name;
			}
			const String metatype_spelling = p_expression->get_datatype().to_string();
			if (!metatype_spelling.is_empty() && metatype_spelling != FSNativeClass::get_class_static() && metatype_spelling != "Object") {
				return metatype_spelling;
			}
		}
		return _docvalue_from_variant(p_expression->reduced_value);
	}

	switch (p_expression->type) {
		case GDP::Node::ARRAY: {
			const GDP::ArrayNode *array = static_cast<const GDP::ArrayNode *>(p_expression);
			return array->elements.is_empty() ? "[]" : "[...]";
		} break;
		case GDP::Node::CALL: {
			const GDP::CallNode *call = static_cast<const GDP::CallNode *>(p_expression);
			if (call->get_callee_type() == GDP::Node::IDENTIFIER) {
				return call->function_name.operator String() + (call->arguments.is_empty() ? "()" : "(...)");
			}
		} break;
		case GDP::Node::DICTIONARY: {
			const GDP::DictionaryNode *dict = static_cast<const GDP::DictionaryNode *>(p_expression);
			return dict->elements.is_empty() ? "{}" : "{...}";
		} break;
		case GDP::Node::IDENTIFIER: {
			const GDP::IdentifierNode *id = static_cast<const GDP::IdentifierNode *>(p_expression);
			return id->name;
		} break;
		default: {
			// Nothing to do.
		} break;
	}

	return "<unknown>";
}

// Builds the space-separated qualifier list for an annotation declaration. The applicable
// target kinds are emitted as lowercase tokens (matching the reflection vocabulary), with a
// trailing `vararg` token when the declaration accepts a rest parameter. The doc viewer reads
// `vararg` to render the rest parameter and shows the remaining tokens as-is.
static String _annotation_qualifiers(const FSParser::AnnotationDeclarationNode *p_annotation_declaration) {
	String qualifiers;
	const uint32_t targets = p_annotation_declaration->targets;
	static const struct {
		uint32_t bit;
		const char *name;
	} target_names[] = {
		{ FSParser::AnnotationDeclarationNode::TARGET_CLASS, "class" },
		{ FSParser::AnnotationDeclarationNode::TARGET_METHOD, "method" },
		{ FSParser::AnnotationDeclarationNode::TARGET_VARIABLE, "variable" },
		{ FSParser::AnnotationDeclarationNode::TARGET_SIGNAL, "signal" },
		{ FSParser::AnnotationDeclarationNode::TARGET_CONSTANT, "constant" },
		{ FSParser::AnnotationDeclarationNode::TARGET_PARAMETER, "parameter" },
	};
	for (const auto &target : target_names) {
		if (targets & target.bit) {
			if (!qualifiers.is_empty()) {
				qualifiers += " ";
			}
			qualifiers += target.name;
		}
	}
	if (p_annotation_declaration->is_variadic()) {
		if (!qualifiers.is_empty()) {
			qualifiers += " ";
		}
		qualifiers += "vararg";
	}
	return qualifiers;
}

static String _description_from_class_doc_data(const FSParser::ClassDocData &p_doc_data) {
	if (p_doc_data.brief.is_empty()) {
		return p_doc_data.description;
	}
	if (p_doc_data.description.is_empty()) {
		return p_doc_data.brief;
	}
	return p_doc_data.brief + "\n\n" + p_doc_data.description;
}

void FSDocGen::_generate_docs(FoundryScript *p_script, const GDP::ClassNode *p_class) {
	p_script->_clear_doc();

	DocData::ClassDoc &doc = p_script->doc;

	doc.is_script_doc = true;
	doc.is_trait = p_class->is_trait;
	doc.is_enum = p_class->is_enum_file;
	for (const GDP::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		if (trait_use.resolved_trait != nullptr) {
			String trait_type;
			String trait_enum;
			String trait_tuple;
			_doctype_from_gdtype(FSAnalyzer::type_from_metatype(trait_use.resolved_trait->get_datatype()), trait_type, trait_enum, trait_tuple);
			if (!trait_type.is_empty()) {
				doc.used_traits.push_back(trait_type);
				continue;
			}
		}
		doc.used_traits.push_back(trait_use.to_string());
	}

	if ((p_class->is_enum_file || p_class->is_tuple_file) && p_class->get_global_name() != StringName()) {
		// A whole-file enum or tuple declaration is documented as its own page, so it must be
		// named by its global name: the simple declaration name collides across namespaces and
		// does not match the qualified name every generated link uses.
		doc.name = p_class->get_global_name();
	} else if (p_script->local_name == StringName()) {
		// This is an outer unnamed class.
		doc.name = _get_script_name(p_script->get_script_path());
	} else {
		// This is an inner or global outer class.
		doc.name = p_script->local_name;
		if (p_script->_owner) {
			doc.name = p_script->_owner->doc.name + "." + doc.name;
		}
	}

	doc.script_path = p_script->get_script_path();

	if (!p_class->is_enum_file) {
		if (p_script->base.is_valid() && p_script->base->is_valid()) {
			if (!p_script->base->doc.name.is_empty()) {
				doc.inherits = p_script->base->doc.name;
			} else {
				doc.inherits = p_script->base->get_instance_base_type();
			}
		} else if (p_script->native.is_valid()) {
			doc.inherits = p_script->native->get_name();
		}
	}

	doc.brief_description = p_class->doc_data.brief;
	doc.description = p_class->doc_data.description;
	for (const Pair<String, String> &p : p_class->doc_data.tutorials) {
		DocData::TutorialDoc td;
		td.title = p.first;
		td.link = p.second;
		doc.tutorials.append(td);
	}
	doc.is_deprecated = p_class->doc_data.is_deprecated;
	doc.deprecated_message = p_class->doc_data.deprecated_message;
	doc.is_experimental = p_class->doc_data.is_experimental;
	doc.experimental_message = p_class->doc_data.experimental_message;

	auto add_method_doc = [&](const GDP::FunctionNode *p_function) {
		ERR_FAIL_NULL(p_function);
		ERR_FAIL_NULL(p_function->identifier);

		const StringName &func_name = p_function->identifier->name;
		p_script->member_lines[func_name] = p_function->start_line;

		DocData::MethodDoc method_doc;
		method_doc.name = func_name;
		method_doc.description = p_function->doc_data.description;
		method_doc.is_deprecated = p_function->doc_data.is_deprecated;
		method_doc.deprecated_message = p_function->doc_data.deprecated_message;
		method_doc.is_experimental = p_function->doc_data.is_experimental;
		method_doc.experimental_message = p_function->doc_data.experimental_message;

		if (p_function->is_vararg()) {
			if (!method_doc.qualifiers.is_empty()) {
				method_doc.qualifiers += " ";
			}
			method_doc.qualifiers += "vararg";
			method_doc.rest_argument.name = p_function->rest_parameter->identifier->name;
			_doctype_from_gdtype(p_function->rest_parameter->get_datatype(), method_doc.rest_argument.type, method_doc.rest_argument.enumeration, method_doc.rest_argument.tuple_type);
		}
		if (p_function->is_abstract) {
			if (!method_doc.qualifiers.is_empty()) {
				method_doc.qualifiers += " ";
			}
			method_doc.qualifiers += "abstract";
		}
		if (p_function->is_noreturn) {
			if (!method_doc.qualifiers.is_empty()) {
				method_doc.qualifiers += " ";
			}
			method_doc.qualifiers += "noreturn";
		}
		if (p_function->is_static) {
			if (!method_doc.qualifiers.is_empty()) {
				method_doc.qualifiers += " ";
			}
			method_doc.qualifiers += "static";
		}
		if (p_function->is_coroutine) {
			if (!method_doc.qualifiers.is_empty()) {
				method_doc.qualifiers += " ";
			}
			method_doc.qualifiers += "async";
		}

		if (func_name == "_init") {
			method_doc.return_type = "void";
		} else if (p_function->return_type) {
			// `p_function->return_type->get_datatype()` is a metatype.
			_doctype_from_gdtype(p_function->get_datatype(), method_doc.return_type, method_doc.return_enum, method_doc.return_tuple, true);
		} else if (!p_function->body->has_return) {
			// If no `return` statement, then return type is `void`, not `Variant`.
			method_doc.return_type = "void";
		} else {
			method_doc.return_type = "Variant";
		}

		for (const GDP::ParameterNode *parameter : p_function->parameters) {
			DocData::ArgumentDoc arg_doc;
			arg_doc.name = parameter->identifier->name;
			_doctype_from_gdtype(parameter->get_datatype(), arg_doc.type, arg_doc.enumeration, arg_doc.tuple_type);
			if (parameter->initializer != nullptr) {
				arg_doc.default_value = docvalue_from_expression(parameter->initializer, parameter->get_datatype());
			}
			method_doc.arguments.push_back(arg_doc);
		}

		doc.methods.push_back(method_doc);
	};

	// A named tuple declaration is documented like a named enum: one entry keyed by the declared
	// name, carrying the `##` doc comment and one field entry per declared field.
	auto add_tuple_docs = [&](const GDP::TupleNode *p_tuple, const String &p_description_fallback = String()) {
		ERR_FAIL_NULL(p_tuple);
		ERR_FAIL_NULL(p_tuple->identifier);

		const StringName &name = p_tuple->identifier->name;

		p_script->member_lines[name] = p_tuple->start_line;

		DocData::TupleDoc tuple_doc;
		tuple_doc.description = p_tuple->doc_data.description.is_empty() ? p_description_fallback : p_tuple->doc_data.description;
		tuple_doc.is_deprecated = p_tuple->doc_data.is_deprecated;
		tuple_doc.deprecated_message = p_tuple->doc_data.deprecated_message;
		tuple_doc.is_experimental = p_tuple->doc_data.is_experimental;
		tuple_doc.experimental_message = p_tuple->doc_data.experimental_message;

		// The declaration's own type carries the resolved element types; fall back to each field's
		// type annotation when the declaration was never resolved (e.g. an erroring script).
		const GDType tuple_type = FSAnalyzer::type_from_metatype(p_tuple->get_datatype());
		for (int i = 0; i < p_tuple->fields.size(); i++) {
			const GDP::TupleNode::Field &field = p_tuple->fields[i];

			DocData::TupleFieldDoc field_doc;
			if (field.identifier != nullptr) {
				field_doc.name = field.identifier->name;
			}
			// A field type is rendered inside the declaration signature, where no tuple channel
			// travels with it, so it is spelled like any other nested position.
			if (tuple_type.is_tuple() && i < tuple_type.container_element_types.size()) {
				_doctype_from_gdtype_nested(tuple_type.container_element_types[i], field_doc.type, field_doc.enumeration);
			} else if (field.type != nullptr) {
				_doctype_from_gdtype_nested(FSAnalyzer::type_from_metatype(field.type->get_datatype()), field_doc.type, field_doc.enumeration);
			}
			if (field_doc.type.is_empty()) {
				field_doc.type = "Variant";
			}
			tuple_doc.fields.push_back(field_doc);
		}

		doc.tuples[name] = tuple_doc;
	};

	auto add_enum_docs = [&](const GDP::EnumNode *p_enum, const String &p_description_fallback = String(), bool p_requires_resolved_values = false) {
		ERR_FAIL_NULL(p_enum);
		ERR_FAIL_NULL(p_enum->identifier);

		StringName name = p_enum->identifier->name;

		p_script->member_lines[name] = p_enum->start_line;

		DocData::EnumDoc enum_doc;
		enum_doc.description = p_enum->doc_data.description.is_empty() ? p_description_fallback : p_enum->doc_data.description;
		enum_doc.is_deprecated = p_enum->doc_data.is_deprecated;
		enum_doc.deprecated_message = p_enum->doc_data.deprecated_message;
		enum_doc.is_experimental = p_enum->doc_data.is_experimental;
		enum_doc.experimental_message = p_enum->doc_data.experimental_message;
		doc.enums[name] = enum_doc;

		for (const GDP::EnumNode::Value &val : p_enum->values) {
			DocData::ConstantDoc const_doc;
			const_doc.name = val.identifier->name;
			// Enum files keep their enum outside ClassNode::members; until that path
			// is analyzed, avoid documenting default zeroes as real values.
			if (!p_requires_resolved_values || val.resolved) {
				const_doc.value = _docvalue_from_variant(val.value);
				const_doc.is_value_valid = true;
			}
			const_doc.type = "int";
			const_doc.enumeration = name;
			const_doc.description = val.doc_data.description;
			const_doc.is_deprecated = val.doc_data.is_deprecated;
			const_doc.deprecated_message = val.doc_data.deprecated_message;
			const_doc.is_experimental = val.doc_data.is_experimental;
			const_doc.experimental_message = val.doc_data.experimental_message;

			doc.constants.push_back(const_doc);
		}
	};

	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		add_enum_docs(p_class->enum_file_decl, _description_from_class_doc_data(p_class->doc_data), true);
		for (const GDP::FunctionNode *function : p_class->enum_file_decl->functions) {
			add_method_doc(function);
		}
	}

	if (p_class->is_tuple_file && p_class->tuple_file_decl != nullptr) {
		add_tuple_docs(p_class->tuple_file_decl, _description_from_class_doc_data(p_class->doc_data));
	}

	for (const GDP::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDP::ClassNode::Member::CLASS: {
				const GDP::ClassNode *inner_class = member.m_class;
				const StringName &class_name = inner_class->identifier->name;

				p_script->member_lines[class_name] = inner_class->start_line;

				// Recursively generate inner class docs.
				// Needs inner FSs to exist: previously generated in FSCompiler::make_scripts().
				FSDocGen::_generate_docs(*p_script->subclasses[class_name], inner_class);
			} break;

			case GDP::ClassNode::Member::CONSTANT: {
				const GDP::ConstantNode *m_const = member.constant;
				const StringName &const_name = member.constant->identifier->name;

				p_script->member_lines[const_name] = m_const->start_line;

				DocData::ConstantDoc const_doc;
				const_doc.name = const_name;
				const_doc.value = _docvalue_from_variant(m_const->initializer->reduced_value);
				const_doc.is_value_valid = true;
				String constant_tuple;
				_doctype_from_gdtype(m_const->get_datatype(), const_doc.type, const_doc.enumeration, constant_tuple);
				const_doc.description = m_const->doc_data.description;
				const_doc.is_deprecated = m_const->doc_data.is_deprecated;
				const_doc.deprecated_message = m_const->doc_data.deprecated_message;
				const_doc.is_experimental = m_const->doc_data.is_experimental;
				const_doc.experimental_message = m_const->doc_data.experimental_message;
				doc.constants.push_back(const_doc);
			} break;

			case GDP::ClassNode::Member::FUNCTION: {
				add_method_doc(member.function);
			} break;

			case GDP::ClassNode::Member::SIGNAL: {
				const GDP::SignalNode *m_signal = member.signal;
				const StringName &signal_name = m_signal->identifier->name;

				p_script->member_lines[signal_name] = m_signal->start_line;

				DocData::MethodDoc signal_doc;
				signal_doc.name = signal_name;
				signal_doc.description = m_signal->doc_data.description;
				signal_doc.is_deprecated = m_signal->doc_data.is_deprecated;
				signal_doc.deprecated_message = m_signal->doc_data.deprecated_message;
				signal_doc.is_experimental = m_signal->doc_data.is_experimental;
				signal_doc.experimental_message = m_signal->doc_data.experimental_message;

				for (const GDP::ParameterNode *p : m_signal->parameters) {
					DocData::ArgumentDoc arg_doc;
					arg_doc.name = p->identifier->name;
					_doctype_from_gdtype(p->get_datatype(), arg_doc.type, arg_doc.enumeration, arg_doc.tuple_type);
					signal_doc.arguments.push_back(arg_doc);
				}

				doc.signals.push_back(signal_doc);
			} break;

			case GDP::ClassNode::Member::VARIABLE: {
				const GDP::VariableNode *m_var = member.variable;
				const StringName &var_name = m_var->identifier->name;

				p_script->member_lines[var_name] = m_var->start_line;

				DocData::PropertyDoc prop_doc;
				prop_doc.name = var_name;
				prop_doc.description = m_var->doc_data.description;
				prop_doc.is_deprecated = m_var->doc_data.is_deprecated;
				prop_doc.deprecated_message = m_var->doc_data.deprecated_message;
				prop_doc.is_experimental = m_var->doc_data.is_experimental;
				prop_doc.experimental_message = m_var->doc_data.experimental_message;
				_doctype_from_gdtype(m_var->get_datatype(), prop_doc.type, prop_doc.enumeration, prop_doc.tuple_type);

				switch (m_var->property) {
					case GDP::VariableNode::PROP_NONE:
						break;
					case GDP::VariableNode::PROP_INLINE:
						if (m_var->setter != nullptr) {
							prop_doc.setter = m_var->setter->identifier->name;
						}
						if (m_var->getter != nullptr) {
							prop_doc.getter = m_var->getter->identifier->name;
						}
						break;
					case GDP::VariableNode::PROP_SETGET:
						if (m_var->setter_pointer != nullptr) {
							prop_doc.setter = m_var->setter_pointer->name;
						}
						if (m_var->getter_pointer != nullptr) {
							prop_doc.getter = m_var->getter_pointer->name;
						}
						break;
				}

				if (m_var->initializer != nullptr) {
					prop_doc.default_value = docvalue_from_expression(m_var->initializer, m_var->get_datatype());
				}

				prop_doc.overridden = false;

				doc.properties.push_back(prop_doc);
			} break;

			case GDP::ClassNode::Member::ENUM: {
				const GDP::EnumNode *m_enum = member.m_enum;
				add_enum_docs(m_enum);
			} break;

			case GDP::ClassNode::Member::TUPLE: {
				add_tuple_docs(member.m_tuple);
			} break;

			case GDP::ClassNode::Member::ENUM_VALUE: {
				const GDP::EnumNode::Value &m_enum_val = member.enum_value;
				const StringName &name = m_enum_val.identifier->name;

				p_script->member_lines[name] = m_enum_val.identifier->start_line;

				DocData::ConstantDoc const_doc;
				const_doc.name = name;
				const_doc.value = _docvalue_from_variant(m_enum_val.value);
				const_doc.is_value_valid = true;
				const_doc.type = "int";
				const_doc.enumeration = "@unnamed_enums";
				const_doc.description = m_enum_val.doc_data.description;
				const_doc.is_deprecated = m_enum_val.doc_data.is_deprecated;
				const_doc.deprecated_message = m_enum_val.doc_data.deprecated_message;
				const_doc.is_experimental = m_enum_val.doc_data.is_experimental;
				const_doc.experimental_message = m_enum_val.doc_data.experimental_message;
				doc.constants.push_back(const_doc);
			} break;

			default:
				break;
		}
	}

	// Annotation declarations live outside `members` (they are root-only passive metadata), so
	// surface them in their own doc section. Each becomes a `MethodDoc` mirroring how built-in
	// annotations are documented: an `@`-prefixed name, declared parameters with types/defaults,
	// the rest parameter for variadic declarations, and the applicable targets as qualifiers.
	for (const GDP::AnnotationDeclarationNode *annotation_declaration : p_class->annotation_declarations) {
		if (annotation_declaration->identifier == nullptr) {
			continue;
		}

		DocData::MethodDoc annotation_doc;
		annotation_doc.name = "@" + String(annotation_declaration->identifier->name);
		annotation_doc.qualifiers = _annotation_qualifiers(annotation_declaration);
		annotation_doc.description = annotation_declaration->doc_data.description;
		annotation_doc.is_deprecated = annotation_declaration->doc_data.is_deprecated;
		annotation_doc.deprecated_message = annotation_declaration->doc_data.deprecated_message;
		annotation_doc.is_experimental = annotation_declaration->doc_data.is_experimental;
		annotation_doc.experimental_message = annotation_declaration->doc_data.experimental_message;

		for (const GDP::ParameterNode *p : annotation_declaration->parameters) {
			DocData::ArgumentDoc arg_doc;
			arg_doc.name = p->identifier->name;
			_doctype_from_gdtype(p->get_datatype(), arg_doc.type, arg_doc.enumeration, arg_doc.tuple_type);
			if (p->initializer != nullptr) {
				arg_doc.default_value = docvalue_from_expression(p->initializer, p->get_datatype());
			}
			annotation_doc.arguments.push_back(arg_doc);
		}

		if (annotation_declaration->is_variadic()) {
			annotation_doc.rest_argument.name = annotation_declaration->rest_parameter->identifier->name;
			_doctype_from_gdtype(annotation_declaration->rest_parameter->get_datatype(), annotation_doc.rest_argument.type, annotation_doc.rest_argument.enumeration, annotation_doc.rest_argument.tuple_type);
		}

		doc.annotations.push_back(annotation_doc);
	}

	// Add doc to the outer-most class.
	p_script->_add_doc(doc);
}

void FSDocGen::generate_docs(FoundryScript *p_script, const GDP::ClassNode *p_class) {
	_populate_singletons_from_autoload_index();
	_generate_docs(p_script, p_class);
	singletons.clear();
}

// This method is needed for the editor, since during autocompletion the script is not compiled, only analyzed.
void FSDocGen::doctype_from_gdtype(const GDType &p_gdtype, String &r_type, String &r_enum, String &r_tuple, bool p_is_return) {
	_populate_singletons_from_autoload_index();
	_doctype_from_gdtype(p_gdtype, r_type, r_enum, r_tuple, p_is_return);
	singletons.clear();
}
