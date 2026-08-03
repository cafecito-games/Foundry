/**************************************************************************/
/*  fs_parser_data_type.cpp                                               */
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

#include "fs_parser.h"

#include "foundry_script.h"
#include "fs_cache.h"

#include "core/core_constants.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

// This function is used to determine that a type is "built-in" as opposed to native
// and custom classes. So `Variant::NIL`, `Variant::OBJECT` and `Variant::UINT` are excluded:
// `Variant::NIL` - `null` is literal, not a type.
// `Variant::OBJECT` - `Object` should be treated as a class, not as a built-in type.
// `Variant::UINT` - the unsigned carrier has no source spelling yet.
static HashMap<StringName, Variant::Type> builtin_types;
Variant::Type FSParser::get_builtin_type(const StringName &p_type) {
	if (unlikely(builtin_types.is_empty())) {
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			Variant::Type type = (Variant::Type)i;
			if (type != Variant::NIL && type != Variant::OBJECT && type != Variant::UINT) {
				builtin_types[Variant::get_type_name(type)] = type;
			}
		}
	}

	if (builtin_types.has(p_type)) {
		return builtin_types[p_type];
	}
	return Variant::VARIANT_MAX;
}

void FSParser::clear_builtin_type_cache() {
	builtin_types.clear();
}

static String _datatype_signature_type_to_string(const FSParser::DataType &p_type, bool p_nil_is_void = false) {
	if (p_nil_is_void && p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL) {
		return "void";
	}
	return p_type.to_string();
}

static String _method_signature_to_string(const Vector<FSParser::DataType> &p_argument_types, const Vector<FSParser::DataType> &p_rest_parameter_type, const Vector<FSParser::DataType> &p_return_type, bool p_has_return) {
	Vector<String> argument_types;
	for (const FSParser::DataType &argument_type : p_argument_types) {
		argument_types.append(_datatype_signature_type_to_string(argument_type));
	}
	// A rich rest tail renders as the final `...Array[T]` entry, matching the source spelling.
	for (const FSParser::DataType &rest_type : p_rest_parameter_type) {
		argument_types.append("..." + _datatype_signature_type_to_string(rest_type));
	}

	const String arguments = String(", ").join(argument_types);
	if (p_has_return) {
		const String return_type = p_return_type.is_empty() ? "void" : _datatype_signature_type_to_string(p_return_type[0], true);
		return vformat("[[%s], %s]", arguments, return_type);
	}
	return vformat("[[%s]]", arguments);
}

String FSParser::DataType::to_string() const {
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
		// Coroutine[T] is a source-level skin over FSFunctionState. Render the phantom result
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
				result = vformat("%s%s", callable_name, _method_signature_to_string(method_parameter_types, method_rest_parameter_type, method_return_type, true));
				break;
			}
			if (builtin_type == Variant::CALLABLE && signature_is_async) {
				// A bare AsyncCallable (or a reference to an async method) carries the async marker
				// without an explicit signature; still render it as AsyncCallable for readability.
				result = "AsyncCallable";
				break;
			}
			if (builtin_type == Variant::SIGNAL && has_explicit_method_signature) {
				result = vformat("Signal%s", _method_signature_to_string(method_parameter_types, Vector<DataType>(), method_return_type, false));
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
				result = FSNativeClass::get_class_static();
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
		case TUPLE: {
			if (tuple_name != StringName()) {
				result = tuple_name.operator String();
				break;
			}
			// An unnamed tuple prints its structural shape, including field names when the type came
			// from a named declaration that was erased, e.g. `(x: float, y: float)`.
			String elements;
			for (int i = 0; i < container_element_types.size(); i++) {
				if (i > 0) {
					elements += ", ";
				}
				if (i < tuple_field_names.size() && tuple_field_names[i] != StringName()) {
					elements += String(tuple_field_names[i]) + ": ";
				}
				elements += container_element_types[i].to_string();
			}
			result = vformat("(%s)", elements);
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

FSParser::DataType FSParser::DataType::substitute(const DataType &p_type, const HashMap<StringName, DataType> &p_bindings) {
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
	for (int i = 0; i < result.method_rest_parameter_type.size(); i++) {
		result.method_rest_parameter_type.write[i] = substitute(result.method_rest_parameter_type[i], p_bindings);
	}
	return result;
}

// Renders a DataType into the flat hint grammar used by PROPERTY_HINT_CALLABLE_TYPE.
// Mirrors DataType::to_string surface syntax, but leaf script/class/enum names use the same
// name selection as PROPERTY_HINT_ARRAY_TYPE so the result round-trips through type_from_property.
static String _encode_signature_leaf_name(const FSParser::DataType &p_type) {
	switch (p_type.kind) {
		case FSParser::DataType::BUILTIN:
			return Variant::get_type_name(p_type.builtin_type);
		case FSParser::DataType::NATIVE:
			return p_type.native_type;
		case FSParser::DataType::SCRIPT:
			if (p_type.script_type.is_valid() && p_type.script_type->get_global_name() != StringName()) {
				return p_type.script_type->get_global_name();
			}
			return p_type.native_type;
		case FSParser::DataType::CLASS:
			if (p_type.class_type != nullptr && p_type.class_type->get_global_name() != StringName()) {
				return p_type.class_type->get_global_name();
			}
			return p_type.native_type;
		case FSParser::DataType::ENUM:
			return String(p_type.native_type).replace("::", ".");
		default:
			return "Variant";
	}
}

static String _encode_signature_type(const FSParser::DataType &p_type);

// Encodes the phantom result type T of a Coroutine[T] into the flat hint grammar. The bracketed result
// slot mirrors to_string: an absent slot renders as "Variant" and a NIL (void) result as "void", so the
// coroutine round-trips with an awaitable result type. Callers wrap this as "Coroutine[<element>]".
static String _encode_coroutine_result_element(const FSParser::DataType &p_coroutine) {
	if (!p_coroutine.has_container_element_type(0)) {
		return "Variant";
	}
	const FSParser::DataType result_type = p_coroutine.get_container_element_type(0);
	if (result_type.kind == FSParser::DataType::BUILTIN && result_type.builtin_type == Variant::NIL) {
		return "void";
	}
	return _encode_signature_type(result_type);
}

// Encodes a Callable/Signal signature suffix: "[[p0, p1], ret]" for callables, "[[p0, p1]]" for signals.
static String _encode_method_signature_suffix(const FSParser::DataType &p_type, bool p_has_return) {
	Vector<String> params;
	for (const FSParser::DataType &param : p_type.method_parameter_types) {
		params.push_back(_encode_signature_type(param));
	}
	// Only a Callable can carry a rest tail; it is always the final entry so the decoder can find it.
	if (p_has_return && p_type.has_method_rest_parameter_type()) {
		params.push_back("..." + _encode_signature_type(p_type.get_method_rest_parameter_type()));
	}
	const String joined = String(", ").join(params);
	if (p_has_return) {
		String return_name;
		if (p_type.method_return_type.is_empty()) {
			return_name = "void";
		} else {
			const FSParser::DataType return_type = p_type.method_return_type[0];
			if (return_type.kind == FSParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
				return_name = "void";
			} else {
				return_name = _encode_signature_type(return_type);
			}
		}
		return vformat("[[%s], %s]", joined, return_name);
	}
	return vformat("[[%s]]", joined);
}

// Strips the class-handle layer off a `Type[T]` slot, leaving the represented instance type. The
// nullable marker belongs to the handle itself, so it is cleared here and re-applied by the caller.
static FSParser::DataType _signature_type_handle_represented_type(const FSParser::DataType &p_type) {
	FSParser::DataType represented = p_type;
	represented.is_type_handle_annotation = false;
	represented.is_meta_type = false;
	represented.is_pseudo_type = false;
	represented.is_constant = false;
	represented.is_nullable = false;
	return represented;
}

static String _encode_signature_type_base(const FSParser::DataType &p_type) {
	// `Type[T]` denotes a class rather than instances of it. The layer is spelled out explicitly so a
	// handle slot cannot decode back as an instance slot, which would silently accept an instance where
	// a class handle is required.
	if (p_type.is_type_handle_annotation) {
		return vformat("Type[%s]", _encode_signature_type(_signature_type_handle_represented_type(p_type)));
	}
	// Coroutine[T] is a NATIVE skin over FSFunctionState; intercept it before the leaf fallthrough
	// so the bare native name never leaks and the result type T survives the boundary.
	if (p_type.is_coroutine) {
		return vformat("Coroutine[%s]", _encode_coroutine_result_element(p_type));
	}
	if (p_type.kind == FSParser::DataType::BUILTIN) {
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

static String _encode_signature_type(const FSParser::DataType &p_type) {
	String encoded = _encode_signature_type_base(p_type);
	// Preserve the nullable marker so a `T?` slot survives the boundary (mirrors to_string's guard:
	// Variant and the NIL builtin are never marked nullable). Generic type_arguments are not yet
	// encoded; a nested user-generic slot degrades to its bare name (tracked as a follow-up).
	if (p_type.is_nullable && p_type.kind != FSParser::DataType::VARIANT &&
			!(p_type.kind == FSParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL)) {
		encoded += "?";
	}
	return encoded;
}

// Whether an enum hint leaf reconstructs to an identical enum identity on the decode side. The leaf is
// "Base.Member" for native-class/built-in enums and a bare name for global enums. This predicate MUST
// stay in sync with the decoder's `_resolve_hint_enum_leaf` (fs_analyzer.cpp): the encoder only
// emits an enum hint the decoder can rebuild exactly, so a round-tripped enum slot is never turned into
// a false strict mismatch. Global (CoreConstants), native-class (ClassDB), and built-in (Variant) enums
// qualify; a script/class enum without a stable global name in the grammar must cross untyped.
static bool _enum_signature_leaf_round_trips(const String &p_name) {
	if (CoreConstants::is_global_enum(p_name)) {
		return true;
	}
	if (ScriptServer::is_global_class(p_name) && ScriptServer::is_global_class_enum(p_name)) {
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
	const Variant::Type base_builtin = FSParser::get_builtin_type(base);
	return base_builtin < Variant::VARIANT_MAX && Variant::has_enum(base_builtin, enum_name);
}

// True when a signature slot round-trips faithfully through the PROPERTY_HINT_CALLABLE_TYPE decoder.
// Slots that cannot (script/class enum leaves and non-global/nested script-classes reduced to a native
// fallback, unexposed natives, generic type_arguments, type parameters) would decode to a coarser type —
// emitting the hint anyway turns a previously gradual-accepted cross-script callable into a false strict
// mismatch. When any slot is lossy the caller omits the hint, so the callable/signal crosses the boundary
// untyped. Global, native-class, and built-in enum leaves do round-trip (see `_enum_signature_leaf_round_trips`).
static bool _signature_type_is_encodable(const FSParser::DataType &p_type) {
	if (!p_type.type_arguments.is_empty()) {
		return false;
	}
	// Coroutine[T] round-trips as long as its result type T does; the coroutine skin itself is always
	// expressible in the grammar (see _encode_signature_type_base). An absent result is Variant.
	if (p_type.is_coroutine) {
		return !p_type.has_container_element_type(0) || _signature_type_is_encodable(p_type.get_container_element_type(0));
	}
	switch (p_type.kind) {
		case FSParser::DataType::VARIANT:
			return true;
		case FSParser::DataType::BUILTIN:
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
					// Default-argument arity cannot round-trip through the hint grammar, so a callable
					// carrying it must cross untyped to avoid rejecting valid default-arg calls at the
					// script-API boundary.
					if (!p_type.method_info.default_arguments.is_empty()) {
						return false;
					}
					// A vararg callable only round-trips when a rich `...Array[T]` tail describes it and that
					// tail is itself encodable. A MethodInfo-only vararg (native/legacy) has no spelling in
					// the hint grammar, so it still crosses untyped.
					if ((p_type.method_info.flags & METHOD_FLAG_VARARG) != 0) {
						if (p_type.builtin_type != Variant::CALLABLE || !p_type.has_method_rest_parameter_type() ||
								!_signature_type_is_encodable(p_type.get_method_rest_parameter_type())) {
							return false;
						}
					}
					for (const FSParser::DataType &parameter_type : p_type.method_parameter_types) {
						if (!_signature_type_is_encodable(parameter_type)) {
							return false;
						}
					}
					if (p_type.builtin_type == Variant::CALLABLE) {
						for (const FSParser::DataType &return_type : p_type.method_return_type) {
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
		case FSParser::DataType::NATIVE:
			return ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type);
		case FSParser::DataType::SCRIPT:
		case FSParser::DataType::CLASS:
			// A user script/class leaf is encoded by name but always decoded back as a SCRIPT kind, while
			// a local annotation of the same class may resolve to a CLASS handle. Strict signature
			// equality compares kinds, so an encoded user-class slot could be falsely rejected after the
			// boundary. Treat these as non-round-trippable until the decoder/comparison agree on a kind;
			// such callables cross untyped (gradual). Native classes are unaffected and still round-trip.
			return false;
		case FSParser::DataType::ENUM:
			// Global, native-class, and built-in enums encode an identity the decoder rebuilds exactly; a
			// script/class enum has no such name in the flat grammar (mirroring the non-global script/class
			// leaf limitation) and must cross untyped to avoid a false mismatch. A tagged union's payload
			// shape has no spelling at all, so it would decode back as an int-backed enum.
			return !p_type.is_tagged_union && _enum_signature_leaf_round_trips(_encode_signature_leaf_name(p_type));
		case FSParser::DataType::TUPLE:
			// The flat hint grammar has no tuple spelling, so a tuple slot would decode back as a
			// bare Array and turn a valid call into a false strict mismatch. Cross untyped instead.
		case FSParser::DataType::TYPE_PARAMETER:
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			return false;
	}
	return false;
}

// Encodes a Coroutine[T] container element (a typed-array element or a typed-dictionary key/value) into
// the flat element-hint grammar, mirroring the top-level PROPERTY_HINT_COROUTINE_TYPE encoding. A
// faithfully round-trippable result yields "Coroutine[<result>]"; an absent or non-round-trippable
// result degrades to a result-less "Coroutine" rather than masquerading as a concrete "Coroutine[Variant]"
// (matching DataType::to_property_info's top-level lossy handling). The runtime typed-container element
// stays FSFunctionState via the in-memory bytecode descriptor and is unaffected by this hint string;
// only the cross-script analyzer round-trip reads it (see FSAnalyzer::type_from_property).
static String _encode_coroutine_container_element(const FSParser::DataType &p_coroutine) {
	if (p_coroutine.has_container_element_type(0) && _signature_type_is_encodable(p_coroutine.get_container_element_type(0))) {
		return vformat("Coroutine[%s]", _encode_coroutine_result_element(p_coroutine));
	}
	// The bracketed empty slot keeps a result-less coroutine element unambiguous: a bare "Coroutine"
	// element name would collide with an ordinary class named Coroutine (only `Coroutine[...]` is the
	// reserved coroutine syntax), so a class used as Array[Coroutine] must stay decodable as that class.
	return "Coroutine[]";
}

// Spells the class-handle layer of a `Type[T]` slot into the property class name. `PropertyInfo` has no
// way to say "a value denoting this class" rather than "an instance of this class", and collapsing the
// handle to its engine class (`FSNativeClass`/`FoundryScript`) would lose which class it represents, so
// a signature crossing the reflection boundary could no longer tell `Type[Node]` from `Type[Resource]`
// or from `Node`. `FSAnalyzer::type_from_property` decodes the marker back into a class handle. The
// convention mirrors the existing nullable `?` class-name suffix.
String fs_encode_type_handle_property_class_name(const StringName &p_represented_class) {
	return vformat("Type[%s]", p_represented_class);
}

bool fs_decode_type_handle_property_class_name(const String &p_class_name, String &r_represented_class) {
	if (!p_class_name.begins_with("Type[") || !p_class_name.ends_with("]")) {
		return false;
	}
	r_represented_class = p_class_name.substr(5, p_class_name.length() - 6);
	return !r_represented_class.is_empty();
}

static String _encode_type_handle_class_name(const StringName &p_represented_class) {
	return fs_encode_type_handle_property_class_name(p_represented_class == StringName() ? StringName("Object") : p_represented_class);
}

PropertyInfo FSParser::DataType::to_property_info(const String &p_name) const {
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
				// type_from_property), instead of leaking the bare FSFunctionState native name and
				// erasing T to Array[FSFunctionState].
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
					case TUPLE:
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
				// bare FSFunctionState native name the NATIVE switch case emits above. See the array
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
			if (is_type_handle_annotation) {
				result.class_name = _encode_type_handle_class_name(native_type);
			} else if (is_meta_type) {
				result.class_name = FSNativeClass::get_class_static();
			} else if (is_coroutine) {
				// Coroutine[T] is a source-level skin over FSFunctionState. Preserve both the
				// coroutine identity and the phantom result type T across the PropertyInfo boundary via a
				// dedicated hint (mirroring PROPERTY_HINT_ARRAY_TYPE), decoded in type_from_property.
				// Without this the skin leaks as a bare FSFunctionState and a cross-script consumer
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
			if (is_type_handle_annotation) {
				const StringName represented = script_type.is_valid() && script_type->get_global_name() != StringName()
						? script_type->get_global_name()
						: native_type;
				result.class_name = _encode_type_handle_class_name(represented);
			} else if (is_meta_type) {
				result.class_name = script_type.is_valid() ? script_type->get_class_name() : Script::get_class_static();
			} else if (script_type.is_valid() && script_type->get_global_name() != StringName()) {
				result.class_name = script_type->get_global_name();
			} else {
				result.class_name = native_type;
			}
			break;
		case CLASS:
			result.type = Variant::OBJECT;
			if (is_type_handle_annotation) {
				const StringName represented = class_type != nullptr && class_type->get_global_name() != StringName()
						? class_type->get_global_name()
						: native_type;
				result.class_name = _encode_type_handle_class_name(represented);
			} else if (is_meta_type) {
				result.class_name = FoundryScript::get_class_static();
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
		case TUPLE:
			// Tuples erase to a read-only Array at runtime; the precise shape only exists statically,
			// so no container hint is emitted.
			result.type = Variant::ARRAY;
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

bool FSParser::DataType::is_typed_container_type() const {
	return kind == FSParser::DataType::BUILTIN && _variant_type_to_typed_array_element_type(builtin_type) != Variant::NIL;
}

FSParser::DataType FSParser::DataType::get_typed_container_type() const {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.builtin_type = _variant_type_to_typed_array_element_type(builtin_type);
	return type;
}

int FSParser::DataType::get_tuple_field_index(const StringName &p_name) const {
	if (p_name == StringName()) {
		return -1;
	}
	for (int i = 0; i < tuple_field_names.size(); i++) {
		if (tuple_field_names[i] == p_name) {
			return i;
		}
	}
	return -1;
}

bool FSParser::DataType::can_reference(const FSParser::DataType &p_other) const {
	if (p_other.is_meta_type) {
		return false;
	}

	// Tuple identity is erased at runtime (every tuple is a read-only Array), so a tuple test is
	// structural: the shape must match element for element. A tuple never references a plain Array
	// and vice versa, otherwise the immutability guarantee would leak through `is`.
	if (kind == TUPLE || p_other.kind == TUPLE) {
		if (kind != TUPLE || p_other.kind != TUPLE) {
			return false;
		}
		if (container_element_types.size() != p_other.container_element_types.size()) {
			return false;
		}
		for (int i = 0; i < container_element_types.size(); i++) {
			if (!container_element_types[i].can_reference(p_other.container_element_types[i])) {
				return false;
			}
		}
		return true;
	}

	if (builtin_type != p_other.builtin_type) {
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
	if (kind == FSParser::DataType::CLASS && script.is_null()) {
		Error err = OK;
		Ref<FoundryScript> scr = FSCache::get_shallow_script(script_path, err);
		ERR_FAIL_COND_V_MSG(err, false, vformat(R"(Error while getting cache for script "%s".)", script_path));
		script.reference_ptr(scr->find_class(class_type->fqcn));
	}

	Ref<Script> script_other = p_other.script_type;
	if (p_other.kind == FSParser::DataType::CLASS && script_other.is_null()) {
		Error err = OK;
		Ref<FoundryScript> scr = FSCache::get_shallow_script(p_other.script_path, err);
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
