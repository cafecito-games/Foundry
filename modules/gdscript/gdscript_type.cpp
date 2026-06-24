/**************************************************************************/
/*  gdscript_type.cpp                                                     */
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

#include "gdscript_type.h"

#include "gdscript.h"
#include "gdscript_trait_utils.h"

#include "core/object/class_db.h"

static bool _is_signature_builtin_type(Variant::Type p_type) {
	return p_type == Variant::CALLABLE || p_type == Variant::SIGNAL;
}

static bool _property_signature_equal(const PropertyInfo &p_left, const PropertyInfo &p_right) {
	return p_left.type == p_right.type &&
			p_left.class_name == p_right.class_name &&
			p_left.hint == p_right.hint &&
			p_left.hint_string == p_right.hint_string &&
			p_left.usage == p_right.usage;
}

static bool _method_signature_equal(const MethodInfo &p_left, const MethodInfo &p_right) {
	if (!_property_signature_equal(p_left.return_val, p_right.return_val)) {
		return false;
	}
	if (p_left.arguments.size() != p_right.arguments.size()) {
		return false;
	}
	for (int i = 0; i < p_left.arguments.size(); i++) {
		if (!_property_signature_equal(p_left.arguments[i], p_right.arguments[i])) {
			return false;
		}
	}
	return true;
}

static bool _datatype_method_signature_equal(const GDScriptParser::DataType &p_left, const GDScriptParser::DataType &p_right) {
	if (p_left.has_explicit_method_signature && p_right.has_explicit_method_signature) {
		if (p_left.method_parameter_types.size() != p_right.method_parameter_types.size()) {
			return false;
		}
		for (int i = 0; i < p_left.method_parameter_types.size(); i++) {
			if (p_left.method_parameter_types[i] != p_right.method_parameter_types[i]) {
				return false;
			}
		}
		if (p_left.builtin_type == Variant::CALLABLE) {
			if (p_left.method_return_type.size() != p_right.method_return_type.size()) {
				return false;
			}
			for (int i = 0; i < p_left.method_return_type.size(); i++) {
				if (p_left.method_return_type[i] != p_right.method_return_type[i]) {
					return false;
				}
			}
		}
		return true;
	}
	return _method_signature_equal(p_left.method_info, p_right.method_info);
}

static bool _class_has_trait(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_trait) {
	if (p_class == nullptr || p_trait == nullptr) {
		return false;
	}

	if (p_class == p_trait || p_class->fqcn == p_trait->fqcn) {
		return true;
	}

	const StringName trait_name = gdscript_trait_identity_name(p_trait);
	const GDScriptParser::ClassNode *current = p_class;
	while (current != nullptr) {
		for (const GDScriptParser::ClassNode *trait : current->resolved_traits) {
			if (trait == p_trait || trait->fqcn == p_trait->fqcn) {
				return true;
			}
		}

		if (current->base_type.kind == GDScriptParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == GDScriptParser::DataType::SCRIPT && current->base_type.script_type.is_valid()) {
			return current->base_type.script_type->has_script_trait(trait_name);
		} else {
			break;
		}
	}

	return false;
}

GDScriptTypeCompatibility::Result GDScriptTypeCompatibility::check(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source) {
	return check(p_target, p_source, Options());
}

GDScriptTypeCompatibility::Result GDScriptTypeCompatibility::check(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, const Options &p_options) {
	Result result;

	// Preserve the current analyzer behavior for parser bugs: don't add user-facing fallout.
	ERR_FAIL_COND_V_MSG(!p_target.is_set(), Result(true, false, false), "Parser bug (please report): Trying to check compatibility of unset target type");
	ERR_FAIL_COND_V_MSG(!p_source.is_set(), Result(true, false, false), "Parser bug (please report): Trying to check compatibility of unset value type");

	if (p_target.kind == GDScriptParser::DataType::VARIANT) {
		result.compatible = true;
		return result;
	}

	if (p_source.kind == GDScriptParser::DataType::VARIANT) {
		result.compatible = !p_options.strict_dynamic;
		result.requires_runtime_check = true;
		return result;
	}

	if (p_options.strict_null && p_source.is_nullable && !p_target.is_nullable) {
		result.compatible = false;
		return result;
	}

	if (p_target.is_nullable && p_source.kind == GDScriptParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		// A nullable target accepts null regardless of its underlying kind. This must run before the
		// builtin and enum target branches below, which would otherwise reject null for those kinds.
		result.compatible = true;
		return result;
	}

	if (p_target.kind == GDScriptParser::DataType::BUILTIN) {
		result.compatible = p_source.kind == GDScriptParser::DataType::BUILTIN && p_target.builtin_type == p_source.builtin_type;
		if (!result.compatible && p_options.allow_implicit_conversion) {
			result.compatible = Variant::can_convert_strict(p_source.builtin_type, p_target.builtin_type);
			result.uses_implicit_conversion = result.compatible;
		}
		if (!result.compatible && p_target.builtin_type == Variant::INT && p_source.kind == GDScriptParser::DataType::ENUM && !p_source.is_meta_type) {
			// Enum value is also integer.
			result.compatible = true;
		}
		if (result.compatible && p_source.kind == GDScriptParser::DataType::BUILTIN && p_target.builtin_type == p_source.builtin_type && _is_signature_builtin_type(p_target.builtin_type)) {
			if (p_target.has_method_signature && p_source.has_method_signature) {
				result.compatible = _datatype_method_signature_equal(p_target, p_source);
			} else if (p_target.has_method_signature && !p_source.has_method_signature) {
				result.requires_runtime_check = true;
			}
		}
		if (result.compatible && p_target.builtin_type == Variant::ARRAY && p_source.builtin_type == Variant::ARRAY) {
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				Options element_options = p_options;
				element_options.allow_implicit_conversion = false;
				const Result element_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = element_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || element_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || element_result.uses_implicit_conversion;
			}
		}
		if (result.compatible && p_target.builtin_type == Variant::DICTIONARY && p_source.builtin_type == Variant::DICTIONARY) {
			Options element_options = p_options;
			element_options.allow_implicit_conversion = false;
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				const Result key_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = key_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || key_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || key_result.uses_implicit_conversion;
			}
			if (result.compatible && p_target.has_container_element_type(1) && p_source.has_container_element_type(1)) {
				const Result value_result = check(p_target.get_container_element_type(1), p_source.get_container_element_type(1), element_options);
				result.compatible = value_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || value_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || value_result.uses_implicit_conversion;
			}
		}
		return result;
	}

	if (p_target.kind == GDScriptParser::DataType::ENUM) {
		if (p_source.kind == GDScriptParser::DataType::BUILTIN && p_source.builtin_type == Variant::INT) {
			result.compatible = true;
			return result;
		}
		if (p_source.kind == GDScriptParser::DataType::ENUM && p_source.native_type == p_target.native_type) {
			result.compatible = true;
			return result;
		}
		return result;
	}

	if (p_source.kind == GDScriptParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		// null is acceptable in object types in the legacy compatibility rules.
		result.compatible = !p_options.strict_null || p_target.is_nullable;
		return result;
	}

	if (p_target.kind == GDScriptParser::DataType::CLASS && p_target.class_type != nullptr &&
			p_target.class_type->is_trait && !p_target.is_meta_type) {
		if (p_source.kind == GDScriptParser::DataType::CLASS && !p_source.is_meta_type) {
			result.compatible = _class_has_trait(p_source.class_type, p_target.class_type);
			return result;
		}
		if (p_source.kind == GDScriptParser::DataType::SCRIPT && p_source.script_type.is_valid() && !p_source.is_meta_type) {
			result.compatible = p_source.script_type->has_script_trait(gdscript_trait_identity_name(p_target.class_type));
			return result;
		}
		return result;
	}

	StringName src_native;
	Ref<Script> src_script;
	const GDScriptParser::ClassNode *src_class = nullptr;

	switch (p_source.kind) {
		case GDScriptParser::DataType::NATIVE:
			if (p_target.kind != GDScriptParser::DataType::NATIVE) {
				return result;
			}
			if (p_source.is_meta_type) {
				src_native = GDScriptNativeClass::get_class_static();
			} else {
				src_native = p_source.native_type;
			}
			break;
		case GDScriptParser::DataType::SCRIPT:
			if (p_target.kind == GDScriptParser::DataType::CLASS) {
				return result;
			}
			if (p_source.script_type.is_null()) {
				return result;
			}
			if (p_source.is_meta_type) {
				src_native = p_source.script_type->get_class_name();
			} else {
				src_script = p_source.script_type;
				src_native = src_script->get_instance_base_type();
			}
			break;
		case GDScriptParser::DataType::CLASS:
			if (p_source.is_meta_type) {
				src_native = GDScript::get_class_static();
			} else {
				src_class = p_source.class_type;
				const GDScriptParser::ClassNode *base = src_class;
				while (base->base_type.kind == GDScriptParser::DataType::CLASS) {
					base = base->base_type.class_type;
				}
				src_native = base->base_type.native_type;
				src_script = base->base_type.script_type;
			}
			break;
		case GDScriptParser::DataType::TYPE_PARAMETER:
		case GDScriptParser::DataType::VARIANT:
		case GDScriptParser::DataType::BUILTIN:
		case GDScriptParser::DataType::ENUM:
		case GDScriptParser::DataType::RESOLVING:
		case GDScriptParser::DataType::UNRESOLVED:
			break;
	}

	switch (p_target.kind) {
		case GDScriptParser::DataType::NATIVE:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, GDScriptNativeClass::get_class_static());
			} else {
				result.compatible = ClassDB::is_parent_class(src_native, p_target.native_type);
			}
			return result;
		case GDScriptParser::DataType::SCRIPT:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, p_target.script_type->get_class_name());
				return result;
			}
			while (src_script.is_valid()) {
				if (src_script == p_target.script_type) {
					result.compatible = true;
					return result;
				}
				src_script = src_script->get_base_script();
			}
			return result;
		case GDScriptParser::DataType::CLASS:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, GDScript::get_class_static());
				return result;
			}
			while (src_class != nullptr) {
				if (src_class == p_target.class_type || src_class->fqcn == p_target.class_type->fqcn) {
					result.compatible = true;
					return result;
				}
				src_class = src_class->base_type.class_type;
			}
			return result;
		case GDScriptParser::DataType::TYPE_PARAMETER:
		case GDScriptParser::DataType::VARIANT:
		case GDScriptParser::DataType::BUILTIN:
		case GDScriptParser::DataType::ENUM:
		case GDScriptParser::DataType::RESOLVING:
		case GDScriptParser::DataType::UNRESOLVED:
			break;
	}

	return result;
}

bool GDScriptTypeCompatibility::is_compatible(const GDScriptParser::DataType &p_target, const GDScriptParser::DataType &p_source, bool p_allow_implicit_conversion) {
	Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	return check(p_target, p_source, options).compatible;
}
