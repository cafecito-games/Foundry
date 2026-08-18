/**************************************************************************/
/*  fs_function.h                                                         */
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

#include "fs_utility_functions.h"

#include "core/object/ref_counted.h"
#include "core/object/script_function_state.h"
#include "core/object/script_language.h"
#include "core/os/thread.h"
#include "core/string/string_name.h"
#include "core/templates/pair.h"
#include "core/templates/self_list.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/numeric_type.h"
#include "core/variant/variant.h"

class FSInstance;
class FoundryScript;
#ifdef TESTS_ENABLED
namespace FSTests {
class TestFSBytecodeScriptAccessor;
class TestEnumStaticReceiverAccessor;
class TestMissingStaticSelfAccessor;
} //namespace FSTests
#endif // TESTS_ENABLED
#ifdef TOOLS_ENABLED
class FSNameManglerApplication;
class FSNameManglerAnalysis;
#endif

// What a runtime value knows about its own specialization. `script` is the value's actual leaf script
// and `type_arguments` are the arguments reified against THAT leaf's parameters, which is the form the
// inheritance projection table consumes. Both value kinds a script test can see -- an instance and a
// class handle -- reduce to this, so the relation never has to know which one it was given.
struct FSRuntimeSpecializationEvidence {
	Ref<Script> script;
	Vector<ContainerType> type_arguments;

	// The evidence an object carries as a script instance. A raw `Crate.new()` reports its script with
	// an empty argument vector, which is an absence of evidence rather than a claim of anything.
	static FSRuntimeSpecializationEvidence from_instance(Object *p_object);
	// The evidence a value carries as a class handle. A specialized handle reports the arguments it was
	// created with; a bare `Script` resource denotes the class it defines and is always unspecialized,
	// so it reports none.
	static FSRuntimeSpecializationEvidence from_class_handle(Object *p_object);
};

class FSDataType {
public:
	Vector<FSDataType> container_element_types;

	// NOTE: `TYPE_PARAMETER` must remain the last/highest value. The compiled-bytecode loader
	// (`FSBytecodeLoader::decode_data_type`) validates a deserialized kind with `kind > TYPE_PARAMETER`;
	// if a new kind is appended after it, update that upper-bound check to the new last value.
	enum Kind {
		VARIANT, // Can be any type.
		BUILTIN,
		NATIVE,
		SCRIPT,
		FOUNDRY_SCRIPT,
		// A tuple shape. The value erases to a read-only Array, so only `is` tests keep the shape
		// around at runtime; `container_element_types` holds the element types, in declaration order.
		TUPLE,
		TYPE_PARAMETER, // Generic type parameter, erased before execution.
	};

	Kind kind = VARIANT;

	// NOTE: `TYPE_PARAMETER_METHOD` must remain the last/highest value. The compiled-bytecode loader
	// validates a deserialized scope with `type_parameter_scope > TYPE_PARAMETER_METHOD`; update that
	// check if a new scope is appended after it.
	enum TypeParameterScope {
		TYPE_PARAMETER_NONE,
		TYPE_PARAMETER_CLASS,
		TYPE_PARAMETER_METHOD,
	};

	Variant::Type builtin_type = Variant::NIL;
	// Exact width and signedness of an integer slot, lowered from the parser type record. `NONE` means
	// the slot declared no width and is constrained by its carrier alone.
	NumericType numeric_type = NumericType::NONE;
	StringName native_type;
	Script *script_type = nullptr;
	Ref<Script> script_type_ref;
	// Whether this type also accepts null. Builtin and enum kinds reject null otherwise; object kinds
	// already do at runtime, but the flag keeps the information available for them too.
	bool is_nullable = false;
	bool is_type_handle = false;
	bool is_self_type = false;
	bool is_script_trait = false;
	StringName script_trait;

	// For TYPE_PARAMETER kind.
	StringName type_parameter_name;
	int type_parameter_index = -1;
	TypeParameterScope type_parameter_scope = TYPE_PARAMETER_NONE;

	// Type arguments of a specialized type handle, e.g. the `int` in `Box[int]`. Empty for unspecialized types.
	Vector<FSDataType> type_arguments;

	_FORCE_INLINE_ bool has_type() const { return kind != VARIANT; }

	// The FoundryScript source spelling of this type, reconstructed from the stored descriptor alone and
	// never from a runtime value. Integer slots name their declared width (`int`, `uint`, `long`,
	// `ulong`); a slot that stores only a carrier decodes to the wide spelling, matching the width
	// erasure a plain `PropertyInfo` round trip has. Containers, generic arguments, tuples, and
	// nullability recurse through the same rule, so `Array[int]` and `Array[long]` stay distinguishable.
	String get_source_type_name() const;

	bool is_type_handle_type(const Variant &p_variant) const;
	static FSDataType from_type_handle_container_type(const ContainerType &p_container_type);
	// Value-position counterpart of the above: rebuilds a plain type (not a type handle) from the
	// container-type description a constant carries.
	static FSDataType from_container_type(const ContainerType &p_container_type);

	// True when `p_base` (or any of its base scripts) is retroactively conformed to trait `p_trait` via
	// an external `extend ... uses` declaration recorded in the conformance registry. Used as a runtime
	// fallback so a retroactively-conformed value satisfies a trait-typed slot even though the trait is
	// absent from the script's own trait list.
	static bool _script_conforms_to_trait(const Ref<Script> &p_base, const StringName &p_trait);

	// True when native engine class `p_native_class` (or any ancestor) is retroactively conformed to
	// trait `p_trait` via an external `extend <native class> uses` declaration. Lets a scriptless native
	// object satisfy a trait-typed slot at runtime.
	static bool _native_class_conforms_to_trait(const StringName &p_native_class, const StringName &p_trait);
	static bool _builtin_type_conforms_to_trait(Variant::Type p_builtin_type, const StringName &p_trait);

	// The one specialization relation behind every script-typed boundary: the `is` operator, the tuple
	// element test, the member and return stores, the dynamic call boundary and the proxy. The caller
	// has already answered the nominal question; this answers only whether the value's reified
	// arguments are the expected ones, for the expected base.
	//
	// The value's own arguments are expressed against ITS leaf's parameters, so they are first projected
	// onto `p_expected_script`'s parameters through the leaf's per-ancestor binding table: a subclass
	// that fixes or forwards a base argument thereby proves that base's specialization.
	//
	// `p_narrowing` selects the rule, and the two differ deliberately. A narrowing comparison -- the
	// `is` operator, whose success narrows the value and lets the guarded body read it at the
	// specialization -- demands positive evidence: same arity, every projected position `EXACT`, none
	// conflicting. A gradual comparison -- every store -- rejects only a position that is known and
	// conflicts, so a raw instance, an unspecialized leaf or an unresolved chain step carries no
	// evidence and is accepted. That is the rule `ContainerTypeValidate::_internal_validate_object()`
	// already applies to a typed container, and it is what keeps the store laxer than the test.
	static bool specialization_matches(const Vector<ContainerType> &p_expected_arguments,
			const Ref<Script> &p_expected_script, const FSRuntimeSpecializationEvidence &p_actual,
			bool p_narrowing);

	// The same relation for a trait target, from whichever evidence the value carries: the binding table
	// a declared `uses Trait[args]` clause fills, or the arguments a retroactive `extend ... uses
	// Trait[args]` recorded. Gradual acceptance is reached only when neither source has anything to say.
	static bool trait_specialization_matches(const Vector<ContainerType> &p_expected_arguments,
			const Ref<Script> &p_expected_script, const StringName &p_trait_name, Object *p_object,
			const Variant &p_value, bool p_narrowing);

	// True when `p_object`'s reified specialization satisfies this type's `type_arguments`, under the
	// rule `p_narrowing` selects. Only reached when `type_arguments` is non-empty, so a non-generic slot
	// pays nothing for it.
	bool _script_specialization_matches(Object *p_object, const Variant &p_value, bool p_narrowing) const;

	// The two halves of the tuple acceptance rule, exposed so the runtime's rejected-store diagnostic
	// asks the same question `is_type()`'s `TUPLE` branch asks instead of re-deriving it. A diagnostic
	// that walks its own copy of the rule can name a culprit the real test does not have, and this
	// subsystem has already paid for that class of duplication several times.
	//
	// Carrier and arity: named identity is erased at runtime, so the test is structural -- an Array of
	// the right arity. Any shape-compatible Array therefore satisfies a named tuple test, the same class
	// of erasure that int-backed enum tests already have.
	bool tuple_carrier_matches(const Variant &p_value) const {
		if (kind != TUPLE || p_value.get_type() != Variant::ARRAY) {
			return false;
		}
		return Array(p_value).size() == container_element_types.size();
	}

	// One element against its declared element type, this type being that element type.
	bool accepts_as_tuple_element(const Variant &p_element, bool p_narrowing = false) const {
		// Object kinds accept null for assignment compatibility, which an `is` test must not:
		// `null is Node` is false, so a null element only satisfies a nullable slot.
		if (p_element.get_type() == Variant::NIL && !is_nullable &&
				(kind == NATIVE || kind == SCRIPT || kind == FOUNDRY_SCRIPT)) {
			return false;
		}
		return is_type(p_element, false, p_narrowing);
	}

	// `p_narrowing` is threaded down from the `is` operator through the `TUPLE` element recursion; every
	// store leaves it at the default. It selects the specialization rule only: a declared integer width
	// is fully observable on the value, so the `BUILTIN` branch enforces it in both modes.
	bool is_type(const Variant &p_variant, bool p_allow_implicit_conversion = false, bool p_narrowing = false) const {
		if (is_nullable && p_variant.get_type() == Variant::NIL) {
			return true;
		}
		if (is_type_handle) {
			return is_type_handle_type(p_variant);
		}
		switch (kind) {
			case VARIANT: {
				return true;
			} break;
			case BUILTIN: {
				Variant::Type var_type = p_variant.get_type();
				bool valid = builtin_type == var_type;
				if (valid && numeric_type != NumericType::NONE) {
					// A declared width is part of the type: a value on the right carrier whose magnitude the
					// width cannot hold is not a value of that type, and no conversion can rescue it -- the
					// carrier already matches, so constructing it again would yield the same magnitude. This
					// returns instead of clearing `valid` so the implicit-conversion fallback below cannot
					// re-admit the value unchanged. A typed-container element never reaches here; its width
					// travels inside the `ContainerType` comparison the array and dictionary cases make.
					return numeric_type_contains(numeric_type, p_variant);
				}
				if (valid && builtin_type == Variant::ARRAY && has_container_element_type(0)) {
					Array array = p_variant;
					valid = array.is_typed() && container_element_types[0].is_same_container_type(array.get_element_type());
				} else if (valid && builtin_type == Variant::DICTIONARY && has_container_element_types()) {
					Dictionary dictionary = p_variant;
					if (dictionary.is_typed()) {
						FSDataType key = get_container_element_type_or_variant(0);
						FSDataType value = get_container_element_type_or_variant(1);
						valid = key.is_same_container_type(dictionary.get_key_type()) && value.is_same_container_type(dictionary.get_value_type());
					} else {
						valid = false;
					}
				} else if (!valid && p_allow_implicit_conversion) {
					valid = Variant::can_convert_strict(var_type, builtin_type);
				}
				return valid;
			} break;
			case NATIVE: {
				if (p_variant.get_type() == Variant::NIL) {
					return true;
				}
				if (p_variant.get_type() != Variant::OBJECT) {
					return false;
				}

				bool was_freed = false;
				Object *obj = p_variant.get_validated_object_with_check(was_freed);
				if (!obj) {
					return !was_freed;
				}

				if (!ClassDB::is_parent_class(obj->get_class_name(), native_type)) {
					return false;
				}
				return true;
			} break;
			case SCRIPT:
			case FOUNDRY_SCRIPT: {
				if (p_variant.get_type() == Variant::NIL) {
					return true;
				}
				if (is_script_trait && p_variant.get_type() != Variant::OBJECT) {
					if (!_builtin_type_conforms_to_trait(p_variant.get_type(), script_trait)) {
						return false;
					}
					if (type_arguments.is_empty()) {
						return true;
					}
					return _script_specialization_matches(nullptr, p_variant, p_narrowing);
				}
				if (p_variant.get_type() != Variant::OBJECT) {
					return false;
				}

				bool was_freed = false;
				Object *obj = p_variant.get_validated_object_with_check(was_freed);
				if (!obj) {
					return !was_freed;
				}

				Ref<Script> base = obj && obj->get_script_instance() ? obj->get_script_instance()->get_script() : nullptr;
				if (is_script_trait) {
					bool conforms = base.is_valid() && (base->has_script_trait(script_trait) || _script_conforms_to_trait(base, script_trait));
					if (!conforms) {
						// A native object (no Foundry Script instance), or a scripted object whose engine base
						// class was retroactively conformed, satisfies a trait-typed slot via the registry.
						conforms = _native_class_conforms_to_trait(obj->get_class_name(), script_trait);
					}
					if (!conforms || type_arguments.is_empty()) {
						return conforms;
					}
					// A specialized trait slot asks a second, invariant question about the arguments the
					// value's implementer conformed with. `Keeper[int]` and `Keeper[String]` are the same
					// trait nominally and different types here.
					return _script_specialization_matches(obj, p_variant, p_narrowing);
				}

				bool valid = false;
				while (base.is_valid()) {
					if (base == script_type) {
						valid = true;
						break;
					}
					base = base->get_base_script();
				}
				if (!valid || type_arguments.is_empty()) {
					return valid;
				}
				// The declared specialization is part of the type, so a nominally correct value whose
				// reified arguments are someone else's is not a value of it. There is no conversion
				// fallback to guard against here the way the `BUILTIN` branch has: an object reaches this
				// answer as itself, and a caller that retries through `Variant::construct(OBJECT, ...)`
				// reconstructs the same object with the same script instance and the same reified
				// arguments, so the retest reproduces this answer rather than rescuing the value.
				return _script_specialization_matches(obj, p_variant, p_narrowing);
			} break;
			case TUPLE: {
				// The structural carrier test plus each element's own type test, both asked through
				// the shared helpers above so the runtime's rejected-store diagnostic can ask exactly
				// the same questions when it hunts for the element that failed.
				if (!tuple_carrier_matches(p_variant)) {
					return false;
				}
				const Array array = p_variant;
				for (int i = 0; i < container_element_types.size(); i++) {
					if (!container_element_types[i].accepts_as_tuple_element(array[i], p_narrowing)) {
						return false;
					}
				}
				return true;
			} break;
			case TYPE_PARAMETER: {
				// Type parameters are erased before execution; accept any value defensively.
				return true;
			} break;
		}
		return false;
	}

	bool can_contain_object() const {
		if (kind == BUILTIN) {
			switch (builtin_type) {
				case Variant::ARRAY:
					if (has_container_element_type(0)) {
						return container_element_types[0].can_contain_object();
					}
					return true;
				case Variant::DICTIONARY:
					if (has_container_element_types()) {
						return get_container_element_type_or_variant(0).can_contain_object() || get_container_element_type_or_variant(1).can_contain_object();
					}
					return true;
				case Variant::NIL:
				case Variant::OBJECT:
					return true;
				default:
					return false;
			}
		}
		return true;
	}

	// True when this type, or any type nested within it (a specialized handle's type arguments, a
	// typed container's element types), denotes `Self`. Used to prove that a call site with no static
	// receiver descriptor to offer -- such as enum dispatch, which has no inheritance chain and
	// therefore no late-bound receiver -- can never observe a type that needed one.
	bool references_self_type() const {
		if (is_self_type) {
			return true;
		}
		for (const FSDataType &type_argument : type_arguments) {
			if (type_argument.references_self_type()) {
				return true;
			}
		}
		for (const FSDataType &element_type : container_element_types) {
			if (element_type.references_self_type()) {
				return true;
			}
		}
		return false;
	}

	void set_container_element_type(int p_index, const FSDataType &p_element_type) {
		ERR_FAIL_COND(p_index < 0);
		while (p_index >= container_element_types.size()) {
			container_element_types.push_back(FSDataType());
		}
		container_element_types.write[p_index] = FSDataType(p_element_type);
	}

	FSDataType get_container_element_type(int p_index) const {
		ERR_FAIL_INDEX_V(p_index, container_element_types.size(), FSDataType());
		return container_element_types[p_index];
	}

	FSDataType get_container_element_type_or_variant(int p_index) const {
		if (p_index < 0 || p_index >= container_element_types.size()) {
			return FSDataType();
		}
		return container_element_types[p_index];
	}

	bool has_container_element_type(int p_index) const {
		return p_index >= 0 && p_index < container_element_types.size();
	}

	bool has_container_element_types() const {
		return !container_element_types.is_empty();
	}

	ContainerType to_container_type() const {
		ContainerType type;
		if (is_nullable) {
			// Core typed containers cannot express "this type or null", so a nullable element type
			// becomes an untyped element. The analyzer still enforces element types statically.
			return type;
		}
		type.builtin_type = builtin_type;
		type.numeric_type = numeric_type;
		if (builtin_type == Variant::OBJECT) {
			type.class_name = native_type;
			if (script_type_ref.is_valid()) {
				type.script = script_type_ref;
			} else if (script_type != nullptr) {
				type.script.reference_ptr(script_type);
			}
			type.is_type_handle = is_type_handle;
		}
		for (const FSDataType &element_type : container_element_types) {
			type.element_types.push_back(element_type.to_container_type());
		}
		for (const FSDataType &argument_type : type_arguments) {
			type.type_arguments.push_back(argument_type.to_container_type());
		}
		return type;
	}

	bool is_same_container_type(const ContainerType &p_type) const {
		return to_container_type() == p_type;
	}

	FSDataType() = default;

	bool operator==(const FSDataType &p_other) const {
		return kind == p_other.kind &&
				builtin_type == p_other.builtin_type &&
				numeric_types_agree(numeric_type, p_other.numeric_type) &&
				native_type == p_other.native_type &&
				is_nullable == p_other.is_nullable &&
				is_type_handle == p_other.is_type_handle &&
				is_self_type == p_other.is_self_type &&
				(script_type == p_other.script_type || script_type_ref == p_other.script_type_ref) &&
				is_script_trait == p_other.is_script_trait &&
				script_trait == p_other.script_trait &&
				container_element_types == p_other.container_element_types &&
				type_parameter_name == p_other.type_parameter_name &&
				type_parameter_scope == p_other.type_parameter_scope &&
				type_parameter_index == p_other.type_parameter_index &&
				type_arguments == p_other.type_arguments;
	}

	bool operator!=(const FSDataType &p_other) const {
		return !(*this == p_other);
	}

	void operator=(const FSDataType &p_other) {
		kind = p_other.kind;
		builtin_type = p_other.builtin_type;
		numeric_type = p_other.numeric_type;
		native_type = p_other.native_type;
		script_type = p_other.script_type;
		script_type_ref = p_other.script_type_ref;
		is_nullable = p_other.is_nullable;
		is_type_handle = p_other.is_type_handle;
		is_self_type = p_other.is_self_type;
		is_script_trait = p_other.is_script_trait;
		script_trait = p_other.script_trait;
		container_element_types = p_other.container_element_types;
		type_parameter_name = p_other.type_parameter_name;
		type_parameter_index = p_other.type_parameter_index;
		type_parameter_scope = p_other.type_parameter_scope;
		type_arguments = p_other.type_arguments;
	}

	FSDataType(const FSDataType &p_other) {
		*this = p_other;
	}

	~FSDataType() {}
};

// The canonical runtime carrier of a value accepted into a tuple-shaped slot: an untyped, read-only
// Array at every tuple level of `p_shape`. A value that already is one is returned as itself, so the
// overwhelmingly common case -- a value `OPCODE_CONSTRUCT_TUPLE` built -- allocates nothing.
//
// Every boundary a value crosses into a tuple slot normalizes through this, not just the in-body
// store: a parameter, a rest element, and a reflective member write all promise the same read-only,
// untyped carrier, and a value that kept the caller's mutable Array would break that promise as soon
// as the caller wrote to it again. Defined beside the store in `fs_vm.cpp`, whose rule it is.
Variant fs_canonical_tuple_value(const FSDataType &p_shape, const Variant &p_value);

// The exact class handle a static call was made through, delivered to the frame it starts.
//
// Static dispatch may select an implementation declared on an ancestor, on a retroactive-conformance
// target, or in an unrelated file. This descriptor records the receiver the call actually began on, so
// a frame can tell `Derived.make()` from `Base.make()` even though both execute the same compiled
// function. It is distinct from instance `self` (absent in a static call) and from the conformance
// target used to validate a witness declaration.
//
// Every field is a value owned by one invocation. Nothing here aliases mutable `FSFunction`, AST,
// constant-pool, or bytecode state, so the same inherited function can execute concurrently through
// different receivers without any shared write.
// Non-owning mirror of `ContainerType` for receiver descriptors. A descriptor must not own any script
// it describes: a script that stores its own specialized receiver in a static variable would otherwise
// close a reference cycle nothing tears down (script -> static storage -> descriptor -> type-argument
// script -> script). Scripts are held by `ObjectID` at every nesting depth -- in `type_arguments` and
// in `element_types` -- and a `ContainerType` is materialized on demand for callers that need a strong
// reference. A dangling id materializes to a `ContainerType` whose `script` is null; consumers that
// resolve a descriptor for use must report that as a freed/missing argument instead of silently
// substituting the bare class or `Variant` (see `FSStaticSelfContext::is_fully_live()`).
struct FSWeakContainerType {
	Variant::Type builtin_type = Variant::NIL;
	NumericType numeric_type = NumericType::NONE;
	StringName class_name;
	ObjectID script_id;
	Vector<FSWeakContainerType> element_types;
	Vector<FSWeakContainerType> type_arguments;
	bool is_type_handle = false;

	static FSWeakContainerType from_container_type(const ContainerType &p_type);
	// Materializes the strong form. A `script_id` that no longer resolves yields a null `Ref<Script>`;
	// the caller decides whether that is a freed argument it must diagnose.
	ContainerType to_container_type() const;
	// True when this node and every descendant still resolve the script they describe. A node that
	// never carried a script (a builtin or native class) is live.
	bool is_fully_live() const;
	String get_type_name() const;
	bool operator==(const FSWeakContainerType &p_other) const;
	_FORCE_INLINE_ bool operator!=(const FSWeakContainerType &p_other) const { return !(*this == p_other); }
};

class FSStaticSelfContext {
public:
	enum Kind {
		NONE,
		NATIVE_CLASS,
		SCRIPT,
		BUILTIN_TYPE,
	};

private:
	Kind kind = NONE;
	StringName native_class;
	// Non-owning, like `CallState`'s script and instance. A live call borrows the descriptor from a
	// caller that already holds the receiver, and a suspended call must not pin it: a script can hold
	// its own suspended state in a static variable, and an owning reference would close a cycle that
	// nothing tears down. A receiver freed while a call is suspended resolves to null, which the
	// runtime must treat as a missing receiver rather than silently substituting another class.
	ObjectID script_id;
	// Concrete arguments of a specialized generic receiver, e.g. the `ImageTexture` of
	// `Crate[ImageTexture]`. Held as `FSWeakContainerType`, not `ContainerType`, for the same reason
	// `script_id` is held instead of a `Ref<Script>`: an argument script can be the same script that
	// owns the descriptor (e.g. `static var handle := Crate[Self]`), so owning it would close a cycle.
	// Empty for an unspecialized script receiver.
	Vector<FSWeakContainerType> type_arguments;
	Variant::Type builtin_type = Variant::NIL;

public:
	static FSStaticSelfContext for_native_class(const StringName &p_class_name);
	static FSStaticSelfContext for_script(const Ref<Script> &p_script);
	static FSStaticSelfContext for_specialized_script(const Ref<Script> &p_script, const Vector<ContainerType> &p_type_arguments);
	static FSStaticSelfContext for_specialized_script(const Ref<Script> &p_script, const Vector<FSWeakContainerType> &p_type_arguments);
	static FSStaticSelfContext for_builtin_type(Variant::Type p_builtin_type);

	_FORCE_INLINE_ Kind get_kind() const { return kind; }
	_FORCE_INLINE_ bool is_valid() const { return kind != NONE; }
	_FORCE_INLINE_ const StringName &get_native_class() const { return native_class; }
	// Null when the receiver script was freed while a call was suspended.
	Ref<Script> get_script() const;
	// Materializes the concrete arguments of a specialized receiver. Returns by value because the
	// descriptor stores them weakly; a still-live argument script becomes a strong `Ref<Script>` here.
	Vector<ContainerType> get_type_arguments() const;
	_FORCE_INLINE_ Variant::Type get_builtin_type() const { return builtin_type; }
	// True when the receiver and every type-argument script (at any nesting depth) is still reachable.
	// A descriptor that references a freed script resolves to a missing receiver rather than a silent
	// substitution.
	bool is_fully_live() const;

	// Resets the descriptor to no receiver. The arguments are stored weakly, so this only drops the
	// descriptions; it never releases a script the descriptor never owned.
	void clear();

	bool operator==(const FSStaticSelfContext &p_other) const;
	_FORCE_INLINE_ bool operator!=(const FSStaticSelfContext &p_other) const { return !(*this == p_other); }

	// Human-readable receiver name, used by runtime diagnostics that must name the exact
	// specialization a call was made through.
	String get_type_name() const;

	// The receiver as an instance-position type descriptor. Returns false when there is no receiver, or
	// when the script receiver of a suspended call was freed before it resumed.
	bool to_data_type(FSDataType &r_type) const;

	// The single substitution of a call frame's receiver into runtime type metadata. Every position
	// that lowering marked as having come from `Self` -- at any nesting depth, through specialized
	// type arguments and typed container element types -- is replaced by `p_context`'s exact receiver,
	// keeping the position's own class-handle and nullability layers.
	//
	// Returns false when a position needs a receiver and `p_context` is absent or describes a freed
	// one. Callers must report that as an error: falling back to the declaration target, the
	// conformance target, or Variant would turn a broken dispatch path into a silent wrong answer.
	static bool resolve_self(const FSDataType &p_type, const FSStaticSelfContext *p_context, FSDataType &r_resolved);
};

class FSFunction {
public:
	// Set on the builtin-type operand of OPCODE_ASSIGN_TYPED_BUILTIN / OPCODE_RETURN_TYPED_BUILTIN to mark
	// the target as nullable, so a null source is stored as-is instead of being rejected or converted.
	// The flag sits well above Variant::VARIANT_MAX, so the real type is recovered by masking it off.
	static constexpr int NULLABLE_TYPE_OPERAND_FLAG = 1 << 24;

	// Each opcode's operand layout lives in THREE places that must stay in sync: the VM's dispatch in
	// `fs_vm.cpp` (authoritative), the `disassemble()` walk in `fs_disassembler.cpp`, and the link-time
	// bounds checker in `FSBytecodeVerifier::verify_function`. Adding, removing, or changing the
	// operands of any opcode requires updating all three and bumping `FSBytecodeFormat::FORMAT_VERSION`
	// (the verifier has no default fall-through: an opcode with no matching case is rejected as corrupt).
	// Operand flags of `OPCODE_ASSIGN_TYPED_CLASS_PARAMETER`. Both describe the *declared* slot, which
	// the resolved shape alone cannot recover: a bare `T` reified to `Array[int]` looks like a declared
	// `Array[T]` once resolved, and a `Type[...]` layer is dropped from the shape so an argument with no
	// handle form still resolves.
	enum AssignTypedClassParameterFlags {
		ASSIGN_TYPED_CLASS_PARAMETER_TYPE_HANDLE = 1 << 0,
		ASSIGN_TYPED_CLASS_PARAMETER_ERASED_CONTAINER = 1 << 1,
	};

	enum Opcode {
		OPCODE_OPERATOR,
		OPCODE_OPERATOR_VALIDATED,
		// Checked integer arithmetic. Unlike the generic and validated operators, these carry the
		// exact width the operation is checked at and refuse a result that width cannot represent
		// instead of wrapping. They write their destination only after the operation succeeds, so a
		// failing compound assignment leaves its target untouched.
		OPCODE_NUMERIC_BINARY,
		OPCODE_NUMERIC_UNARY,
		// Checked integer conversion, including the unsigned carrier that `Variant::construct()` has
		// no constructor for.
		OPCODE_NUMERIC_CAST,
		// Unchecked bit reinterpret between equal-width integer types (the `as!` operator): masks the
		// source pattern to the target width and lays it on the target carrier without a range check.
		OPCODE_NUMERIC_REINTERPRET,
		OPCODE_TYPE_TEST_BUILTIN,
		OPCODE_TYPE_TEST_ARRAY,
		OPCODE_TYPE_TEST_DICTIONARY,
		OPCODE_TYPE_TEST_TUPLE, // Structural shape test: Array, matching arity, per-element type test.
		// Membership test against an enum's value set (int-backed or tagged union).
		OPCODE_TYPE_TEST_ENUM,
		// Tagged-union case test that also binds the case payload on success. Like OPCODE_TYPE_TEST_TUPLE,
		// the test is structural: a tagged-union value erases to `[tag, payload...]` with no nominal
		// identity, so a statically untyped operand carrying the same tag and arity from an unrelated
		// union matches. Statically typed operands are still checked nominally by the analyzer.
		OPCODE_TYPE_TEST_ENUM_CASE,
		OPCODE_TYPE_TEST_NATIVE,
		OPCODE_TYPE_TEST_SCRIPT,
		OPCODE_SET_KEYED,
		OPCODE_SET_KEYED_VALIDATED,
		OPCODE_SET_INDEXED_VALIDATED,
		OPCODE_GET_KEYED,
		OPCODE_GET_KEYED_VALIDATED,
		OPCODE_GET_INDEXED_VALIDATED,
		OPCODE_SET_NAMED,
		OPCODE_SET_NAMED_VALIDATED,
		OPCODE_GET_NAMED,
		OPCODE_GET_NAMED_VALIDATED,
		OPCODE_SET_MEMBER,
		OPCODE_GET_MEMBER,
		OPCODE_GET_TYPE_PARAMETER, // Reified script bound to the enclosing class's i-th type parameter.
		OPCODE_SET_STATIC_VARIABLE, // Only for FoundryScript.
		OPCODE_GET_STATIC_VARIABLE, // Only for FoundryScript.
		OPCODE_ASSIGN,
		OPCODE_ASSIGN_NULL,
		OPCODE_ASSIGN_TRUE,
		OPCODE_ASSIGN_FALSE,
		OPCODE_ASSIGN_TYPED_BUILTIN,
		OPCODE_ASSIGN_TYPED_ARRAY,
		OPCODE_ASSIGN_TYPED_DICTIONARY,
		OPCODE_ASSIGN_TYPED_NATIVE,
		OPCODE_ASSIGN_TYPED_SCRIPT,
		OPCODE_ASSIGN_TYPED_PARAMETER,
		// Store validated against the receiver's reification of the declaring class's type parameters.
		// Emitted for a function-body slot (local, later assignment, or return) whose declared type
		// mentions a class-scope parameter, which erases like any other parameter and would otherwise
		// take the value untested.
		OPCODE_ASSIGN_TYPED_CLASS_PARAMETER,
		// Store validated against a tuple slot's declared shape. A tuple type slot erases to a plain,
		// untyped Array, so the shape travels as a compiled descriptor operand instead: the same
		// descriptor, arity operand, and structural test `OPCODE_TYPE_TEST_TUPLE` uses. The check never
		// converts -- tuple elements are invariant and a tuple value is a read-only Array -- so a value
		// that passes is stored exactly as it arrived.
		OPCODE_ASSIGN_TYPED_TUPLE,
		// Call-site validation of one argument of a statically resolved generic call. The callee is
		// compiled once with its method-scope type parameters erased, so its own argument binding has no
		// run-time type to check the value against; the caller, which knows the substitution, checks and
		// converts here and passes the converted value on. The substituted type travels as a compiled
		// descriptor operand, and the callee name and argument ordinal travel beside it so a rejection
		// names the boundary that rejected it.
		OPCODE_VALIDATE_CALL_ARGUMENT,
		OPCODE_ASSIGN_TYPED_ARRAY_CONVERT,
		OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT,
		OPCODE_CAST_TO_BUILTIN,
		OPCODE_CAST_TO_NATIVE,
		OPCODE_CAST_TO_SCRIPT,
		OPCODE_CONSTRUCT, // Only for basic types!
		OPCODE_CONSTRUCT_VALIDATED, // Only for basic types!
		OPCODE_CONSTRUCT_ARRAY,
		OPCODE_CONSTRUCT_TYPED_ARRAY,
		OPCODE_CONSTRUCT_TUPLE, // Builds the read-only Array a tuple value erases to.
		OPCODE_CONSTRUCT_DICTIONARY,
		OPCODE_CONSTRUCT_TYPED_DICTIONARY,
		OPCODE_CONSTRUCT_SPECIALIZED, // Instantiate a generic script with reified type arguments.
		// Build a specialized class handle whose type arguments name a class type parameter, resolved
		// against the frame's receiver. A handle whose arguments are all concrete stays a constant.
		OPCODE_MAKE_SPECIALIZED_CLASS_HANDLE,
		OPCODE_CALL,
		OPCODE_CALL_RETURN,
		OPCODE_CALL_ASYNC,
		OPCODE_CALL_ENUM,
		OPCODE_CALL_ENUM_RETURN,
		OPCODE_CALL_ENUM_ASYNC,
		OPCODE_CALL_UTILITY,
		OPCODE_CALL_UTILITY_VALIDATED,
		OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY,
		OPCODE_CALL_BUILTIN_TYPE_VALIDATED,
		OPCODE_CALL_SELF_BASE,
		OPCODE_CALL_METHOD_BIND,
		OPCODE_CALL_METHOD_BIND_RET,
		OPCODE_CALL_BUILTIN_STATIC,
		OPCODE_CALL_NATIVE_STATIC,
		OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN,
		OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN,
		OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN,
		OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN,
		OPCODE_AWAIT,
		OPCODE_AWAIT_RESUME,
		OPCODE_CREATE_LAMBDA,
		OPCODE_CREATE_SELF_LAMBDA,
		OPCODE_JUMP,
		OPCODE_JUMP_IF,
		OPCODE_JUMP_IF_NOT,
		OPCODE_JUMP_TO_DEF_ARGUMENT,
		OPCODE_JUMP_IF_SHARED,
		OPCODE_RETURN,
		OPCODE_RETURN_TYPED_BUILTIN,
		OPCODE_RETURN_TYPED_ARRAY,
		OPCODE_RETURN_TYPED_DICTIONARY,
		OPCODE_RETURN_TYPED_NATIVE,
		OPCODE_RETURN_TYPED_SCRIPT,
		OPCODE_ITERATE_BEGIN,
		OPCODE_ITERATE_BEGIN_INT,
		OPCODE_ITERATE_BEGIN_FLOAT,
		OPCODE_ITERATE_BEGIN_VECTOR2,
		OPCODE_ITERATE_BEGIN_VECTOR2I,
		OPCODE_ITERATE_BEGIN_VECTOR3,
		OPCODE_ITERATE_BEGIN_VECTOR3I,
		OPCODE_ITERATE_BEGIN_STRING,
		OPCODE_ITERATE_BEGIN_DICTIONARY,
		OPCODE_ITERATE_BEGIN_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_STRING_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY,
		OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY,
		OPCODE_ITERATE_BEGIN_OBJECT,
		OPCODE_ITERATE_BEGIN_RANGE,
		OPCODE_ITERATE,
		OPCODE_ITERATE_INT,
		OPCODE_ITERATE_FLOAT,
		OPCODE_ITERATE_VECTOR2,
		OPCODE_ITERATE_VECTOR2I,
		OPCODE_ITERATE_VECTOR3,
		OPCODE_ITERATE_VECTOR3I,
		OPCODE_ITERATE_STRING,
		OPCODE_ITERATE_DICTIONARY,
		OPCODE_ITERATE_ARRAY,
		OPCODE_ITERATE_PACKED_BYTE_ARRAY,
		OPCODE_ITERATE_PACKED_INT32_ARRAY,
		OPCODE_ITERATE_PACKED_INT64_ARRAY,
		OPCODE_ITERATE_PACKED_FLOAT32_ARRAY,
		OPCODE_ITERATE_PACKED_FLOAT64_ARRAY,
		OPCODE_ITERATE_PACKED_STRING_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR2_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR3_ARRAY,
		OPCODE_ITERATE_PACKED_COLOR_ARRAY,
		OPCODE_ITERATE_PACKED_VECTOR4_ARRAY,
		OPCODE_ITERATE_OBJECT,
		OPCODE_ITERATE_RANGE,
		OPCODE_STORE_GLOBAL,
		OPCODE_STORE_NAMED_GLOBAL,
		OPCODE_TYPE_ADJUST_BOOL,
		OPCODE_TYPE_ADJUST_INT,
		OPCODE_TYPE_ADJUST_UINT,
		OPCODE_TYPE_ADJUST_FLOAT,
		OPCODE_TYPE_ADJUST_STRING,
		OPCODE_TYPE_ADJUST_VECTOR2,
		OPCODE_TYPE_ADJUST_VECTOR2I,
		OPCODE_TYPE_ADJUST_RECT2,
		OPCODE_TYPE_ADJUST_RECT2I,
		OPCODE_TYPE_ADJUST_VECTOR3,
		OPCODE_TYPE_ADJUST_VECTOR3I,
		OPCODE_TYPE_ADJUST_TRANSFORM2D,
		OPCODE_TYPE_ADJUST_VECTOR4,
		OPCODE_TYPE_ADJUST_VECTOR4I,
		OPCODE_TYPE_ADJUST_PLANE,
		OPCODE_TYPE_ADJUST_QUATERNION,
		OPCODE_TYPE_ADJUST_AABB,
		OPCODE_TYPE_ADJUST_BASIS,
		OPCODE_TYPE_ADJUST_TRANSFORM3D,
		OPCODE_TYPE_ADJUST_PROJECTION,
		OPCODE_TYPE_ADJUST_COLOR,
		OPCODE_TYPE_ADJUST_STRING_NAME,
		OPCODE_TYPE_ADJUST_NODE_PATH,
		OPCODE_TYPE_ADJUST_RID,
		OPCODE_TYPE_ADJUST_OBJECT,
		OPCODE_TYPE_ADJUST_CALLABLE,
		OPCODE_TYPE_ADJUST_SIGNAL,
		OPCODE_TYPE_ADJUST_DICTIONARY,
		OPCODE_TYPE_ADJUST_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY,
		OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY,
		OPCODE_LOAD_STATIC_SELF_CLASS,
		OPCODE_ASSERT,
		OPCODE_BREAKPOINT,
		OPCODE_LINE,
		OPCODE_END
	};

	enum Address {
		ADDR_BITS = 24,
		ADDR_MASK = ((1 << ADDR_BITS) - 1),
		ADDR_TYPE_MASK = ~ADDR_MASK,
		ADDR_TYPE_STACK = 0,
		ADDR_TYPE_CONSTANT = 1,
		ADDR_TYPE_MEMBER = 2,
		ADDR_TYPE_MAX = 3,
	};

	enum FixedAddresses {
		ADDR_STACK_SELF = 0,
		ADDR_STACK_CLASS = 1,
		ADDR_STACK_NIL = 2,
		FIXED_ADDRESSES_MAX = 3,
		ADDR_SELF = ADDR_STACK_SELF | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_CLASS = ADDR_STACK_CLASS | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_NIL = ADDR_STACK_NIL | (ADDR_TYPE_STACK << ADDR_BITS),
	};

	enum ReflectionKind : uint8_t {
		REFLECTION_NONE = 0,
		REFLECTION_METHODS = 1 << 0,
		REFLECTION_PROPERTIES = 1 << 1,
		REFLECTION_SIGNALS = 1 << 2,
	};

	static uint8_t get_reflection_kind(const StringName &p_method, const StringName &p_class = StringName()) {
		if (p_method == SNAME("get_method_list") ||
				(p_class == SNAME("FSReflection") &&
						(p_method == SNAME("get_methods") ||
								p_method == SNAME("get_method_descriptors")))) {
			return REFLECTION_METHODS;
		}
		if (p_method == SNAME("get_property_list") ||
				(p_class == SNAME("FSReflection") &&
						(p_method == SNAME("get_properties") ||
								p_method == SNAME("get_property_descriptors")))) {
			return REFLECTION_PROPERTIES;
		}
		if (p_method == SNAME("get_signal_list")) {
			return REFLECTION_SIGNALS;
		}
		return REFLECTION_NONE;
	}

	struct StackDebug {
		int line;
		int pos;
		bool added;
		StringName identifier;
	};

private:
	friend class FoundryScript;
	friend class FSCompiler;
	friend class FSByteCodeGenerator;
	friend class FSLanguage;
	friend class FSBytecodeExporter;
	friend class FSBytecodeLoader;
	friend class FSBytecodeVerifier;
#ifdef TOOLS_ENABLED
	friend class FSNameManglerApplication;
	friend class FSNameManglerAnalysis;
#endif
#ifdef TESTS_ENABLED
	friend class FSTests::TestFSBytecodeScriptAccessor;
	friend class FSTests::TestEnumStaticReceiverAccessor;
	friend class FSTests::TestMissingStaticSelfAccessor;
#endif // TESTS_ENABLED

	StringName name;
	StringName source;
	bool _static = false;
	Vector<FSDataType> argument_types;
	// Compiled type of the rest ("...") parameter, unset for a non-variadic function. `_vararg_index`
	// stays the arity/stack-slot marker; this only carries the collected Array's element contract so
	// the call prologue can reify and validate it. `MethodInfo` is deliberately unchanged.
	FSDataType rest_parameter_type;
	FSDataType return_type;
	MethodInfo method_info;
	Variant rpc_config;

	FoundryScript *_script = nullptr;
	int _initial_line = 0;
	int _argument_count = 0;
	int _vararg_index = -1;
	int _stack_size = 0;
	int _instruction_args_size = 0;

	SelfList<FSFunction> function_list{ this };
	mutable Variant nil;
	HashMap<int, Variant::Type> temporary_slots;
	List<StackDebug> stack_debug;

	Vector<int> code;
	Vector<int> default_arguments;
	Vector<Variant> constants;
	Vector<StringName> global_names;
	Vector<Variant::ValidatedOperatorEvaluator> operator_funcs;
	Vector<Variant::ValidatedSetter> setters;
	Vector<Variant::ValidatedGetter> getters;
	Vector<Variant::ValidatedKeyedSetter> keyed_setters;
	Vector<Variant::ValidatedKeyedGetter> keyed_getters;
	Vector<Variant::ValidatedIndexedSetter> indexed_setters;
	Vector<Variant::ValidatedIndexedGetter> indexed_getters;
	Vector<Variant::ValidatedBuiltInMethod> builtin_methods;
	Vector<Variant::ValidatedConstructor> constructors;
	Vector<Variant::ValidatedUtilityFunction> utilities;
	Vector<FSUtilityFunctions::FunctionPtr> gds_utilities;
	Vector<MethodBind *> methods;
	Vector<FSFunction *> lambdas;
	Vector<StringName> builtin_method_names;

	int _code_size = 0;
	int _default_arg_count = 0;
	int _constant_count = 0;
	int _global_names_count = 0;
	int _operator_funcs_count = 0;
	int _setters_count = 0;
	int _getters_count = 0;
	int _keyed_setters_count = 0;
	int _keyed_getters_count = 0;
	int _indexed_setters_count = 0;
	int _indexed_getters_count = 0;
	int _builtin_methods_count = 0;
	int _constructors_count = 0;
	int _utilities_count = 0;
	int _gds_utilities_count = 0;
	int _methods_count = 0;
	int _lambdas_count = 0;

	int *_code_ptr = nullptr;
	const int *_default_arg_ptr = nullptr;
	mutable Variant *_constants_ptr = nullptr;
	const StringName *_global_names_ptr = nullptr;
	const Variant::ValidatedOperatorEvaluator *_operator_funcs_ptr = nullptr;
	const Variant::ValidatedSetter *_setters_ptr = nullptr;
	const Variant::ValidatedGetter *_getters_ptr = nullptr;
	const Variant::ValidatedKeyedSetter *_keyed_setters_ptr = nullptr;
	const Variant::ValidatedKeyedGetter *_keyed_getters_ptr = nullptr;
	const Variant::ValidatedIndexedSetter *_indexed_setters_ptr = nullptr;
	const Variant::ValidatedIndexedGetter *_indexed_getters_ptr = nullptr;
	const Variant::ValidatedBuiltInMethod *_builtin_methods_ptr = nullptr;
	const Variant::ValidatedConstructor *_constructors_ptr = nullptr;
	const Variant::ValidatedUtilityFunction *_utilities_ptr = nullptr;
	const FSUtilityFunctions::FunctionPtr *_gds_utilities_ptr = nullptr;
	MethodBind **_methods_ptr = nullptr;
	FSFunction **_lambdas_ptr = nullptr;

#ifdef DEBUG_ENABLED
	CharString func_cname;
	const char *_func_cname = nullptr;

	Vector<String> operator_names;
	Vector<String> setter_names;
	Vector<String> getter_names;
	Vector<String> builtin_methods_names;
	Vector<String> constructors_names;
	Vector<String> utilities_names;
	Vector<String> gds_utilities_names;

	struct Profile {
		StringName signature;
		SafeNumeric<uint64_t> call_count;
		SafeNumeric<uint64_t> self_time;
		SafeNumeric<uint64_t> total_time;
		SafeNumeric<uint64_t> frame_call_count;
		SafeNumeric<uint64_t> frame_self_time;
		SafeNumeric<uint64_t> frame_total_time;
		uint64_t last_frame_call_count = 0;
		uint64_t last_frame_self_time = 0;
		uint64_t last_frame_total_time = 0;
		typedef struct NativeProfile {
			uint64_t call_count;
			uint64_t total_time;
			String signature;
		} NativeProfile;
		HashMap<String, NativeProfile> native_calls;
		HashMap<String, NativeProfile> last_native_calls;
	} profile;
#endif

#ifdef TOOLS_ENABLED

public:
	// Symbolic identities for every process-bound pointer the compiled function stores, recorded
	// by FSByteCodeGenerator so the bytecode exporter can re-resolve the pointers in another
	// process. Index i of each descriptor vector describes entry i of the matching pointer table.
	struct ExportFixups {
		struct OperatorKey {
			Variant::Operator op;
			Variant::Type left_type;
			Variant::Type right_type;
		};
		struct TypedNameKey {
			Variant::Type type;
			StringName name;
		};
		struct ConstructorKey {
			Variant::Type type;
			int constructor_index;
		};
		struct MethodBindKey {
			StringName class_name;
			StringName method_name;
		};
		struct GlobalStore {
			int code_offset; // Index into `code` of the baked global-array operand.
			StringName global_name;
		};
		Vector<OperatorKey> operators;
		Vector<TypedNameKey> setters;
		Vector<TypedNameKey> getters;
		Vector<Variant::Type> keyed_setters;
		Vector<Variant::Type> keyed_getters;
		Vector<Variant::Type> indexed_setters;
		Vector<Variant::Type> indexed_getters;
		Vector<TypedNameKey> builtin_methods;
		Vector<ConstructorKey> constructors;
		Vector<StringName> utilities;
		Vector<StringName> gds_utilities;
		Vector<MethodBindKey> method_binds;
		Vector<GlobalStore> global_stores;
		// Code offsets of every non-validated OPCODE_OPERATOR instruction. The VM patches inline-cache
		// words into that instruction at runtime (an operand signature, a cached return type, and a raw
		// evaluator function pointer); the exporter zeroes those words so no process-local pointer or
		// state is baked into a `.fsb`.
		Vector<int> operator_cache_offsets;
		Vector<StringName> named_globals; // For export-time validation only.
	};
	ExportFixups export_fixups;

private:
	uint8_t self_reflection_kinds = REFLECTION_NONE;
	uint8_t unresolved_reflection_kinds = REFLECTION_NONE;
#endif // TOOLS_ENABLED

	// Mirror of `has_self_referencing_signature()`, refreshed by `setup_runtime_pointers()`.
	bool _references_self_types = false;

	// A tuple slot's declared shape travels to run time as a descriptor constant, and rebuilding it on
	// every store walks a Dictionary tree and allocates a fresh element vector. A descriptor whose whole
	// tree names neither a type parameter nor `Self` decodes to the same shape on every execution, so it
	// is decoded once here and shared by every instruction storing through that constant. A
	// receiver-dependent descriptor has no frame-independent answer and keeps the per-execution path.
	//
	// `_predecoded_tuple_shape_indices` is indexed by constant index and holds an index into
	// `_predecoded_tuple_shapes`, or -1 for a constant that is not a receiver-independent tuple
	// descriptor. Both are derived, never serialized, and immutable once `setup_runtime_pointers()`
	// finishes, so concurrent calls into the same function only ever read them.
	Vector<int> _predecoded_tuple_shape_indices;
	Vector<FSDataType> _predecoded_tuple_shapes;
	const int *_predecoded_tuple_shape_indices_ptr = nullptr;
	const FSDataType *_predecoded_tuple_shapes_ptr = nullptr;
	int _predecoded_tuple_shape_index_count = 0;

	void _build_predecoded_tuple_shapes();

	// The predecoded shape for a tuple store's descriptor operand, or `nullptr` when the descriptor is
	// receiver-dependent and the caller must decode it against the running frame.
	_FORCE_INLINE_ const FSDataType *_predecoded_tuple_shape(int p_address) const {
		if (((p_address & ADDR_TYPE_MASK) >> ADDR_BITS) != ADDR_TYPE_CONSTANT) {
			return nullptr;
		}
		const int constant_index = p_address & ADDR_MASK;
		if (constant_index >= _predecoded_tuple_shape_index_count) {
			return nullptr;
		}
		const int shape_index = _predecoded_tuple_shape_indices_ptr[constant_index];
		return shape_index < 0 ? nullptr : &_predecoded_tuple_shapes_ptr[shape_index];
	}

	static thread_local const FSStaticSelfContext *_current_static_self_context;

	// Scoped installation of the running frame's static receiver descriptor. Restores the caller's
	// descriptor on every exit path, including the one that suspends the frame into an
	// `FSFunctionState`.
	struct StaticSelfContextGuard {
		const FSStaticSelfContext *previous = nullptr;

		explicit StaticSelfContextGuard(const FSStaticSelfContext *p_context) :
				previous(_current_static_self_context) {
			_current_static_self_context = p_context;
		}
		~StaticSelfContextGuard() { _current_static_self_context = previous; }

		StaticSelfContextGuard(const StaticSelfContextGuard &) = delete;
		StaticSelfContextGuard &operator=(const StaticSelfContextGuard &) = delete;
	};

	// Formats a rejected call for the script error log. `p_callee` is the Foundry Script function that
	// rejected the call, when the caller can name one; its compiled parameter descriptors carry the
	// declaration details `Callable::CallError` reduces to a carrier. `p_parameter_index_offset` is added
	// to the reported argument index to reach the callee's own parameter, which differs from it only
	// where the index was re-based: a forwarding call counts the target's name as an argument, and a
	// lambda's reported index has already been moved past the captures the callee declares first.
	String _get_call_error(const String &p_where, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err, const FSFunction *p_callee = nullptr, int p_parameter_index_offset = 0) const;
	String _get_callable_call_error(const String &p_where, const Callable &p_callable, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const;
	Variant _get_default_variant_for_data_type(const FSDataType &p_data_type);
	// Validates one incoming call argument against a compiled parameter/element type and writes the
	// converted value to `r_value`. Returns false and fills `r_err` with
	// `CALL_ERROR_INVALID_ARGUMENT` for `p_argument_index` when the value cannot be accepted; `r_value`
	// is left untouched in that case. Shared by fixed parameters and rest-array elements so both apply
	// the same conversion policy.
	bool _convert_call_argument(const Variant &p_value, const FSDataType &p_type, Variant &r_value,
			Callable::CallError &r_err, int p_argument_index) const;

public:
	static constexpr int MAX_CALL_DEPTH = 2048; // Limit to try to avoid crash because of a stack overflow.

	struct CallState {
		Signal completed;
		FoundryScript *script = nullptr;
		FSInstance *instance = nullptr;
#ifdef DEBUG_ENABLED
		StringName function_name;
		String script_path;
#endif
		Vector<uint8_t> stack;
		int stack_size = 0;
		// A witness-style call has no FSInstance from which `self` can be reconstructed after an
		// await. Keep its explicit receiver alongside the copied non-reserved stack until resume.
		Variant self_override;
		bool has_self_override = false;
		// The receiver the suspended static call began on. Retained so a resumed frame cannot come
		// back with an absent or different specialization.
		FSStaticSelfContext static_self;
		int ip = 0;
		int line = 0;
		int defarg = 0;
		Variant result;
	};

	_FORCE_INLINE_ StringName get_name() const { return name; }
	_FORCE_INLINE_ StringName get_source() const { return source; }
	_FORCE_INLINE_ FoundryScript *get_script() const { return _script; }
	_FORCE_INLINE_ bool is_static() const { return _static; }
	_FORCE_INLINE_ bool is_vararg() const { return _vararg_index >= 0; }
	_FORCE_INLINE_ MethodInfo get_method_info() const { return method_info; }
	_FORCE_INLINE_ const FSDataType &get_return_type() const { return return_type; }
	_FORCE_INLINE_ const FSDataType &get_rest_parameter_type() const { return rest_parameter_type; }
	_FORCE_INLINE_ int get_argument_count() const { return _argument_count; }
	_FORCE_INLINE_ const FSDataType &get_argument_type(int p_index) const { return argument_types[p_index]; }
	_FORCE_INLINE_ Variant get_rpc_config() const { return rpc_config; }
	_FORCE_INLINE_ int get_max_stack_size() const { return _stack_size; }

	Variant get_constant(int p_idx) const;
	StringName get_global_name(int p_idx) const;

	// Re-establishes every `_*_count` / `_*_ptr` mirror field from the backing vectors. The byte
	// code generator and the compiled-bytecode loader both populate the vectors and then call this,
	// so the pointer-table invariants cannot drift between the two producers.
	void setup_runtime_pointers();

#ifdef TOOLS_ENABLED
	_FORCE_INLINE_ const Vector<int> &get_code() const { return code; }
	_FORCE_INLINE_ int get_operator_funcs_count() const { return _operator_funcs_count; }
	_FORCE_INLINE_ int get_setters_count() const { return _setters_count; }
	_FORCE_INLINE_ int get_getters_count() const { return _getters_count; }
	_FORCE_INLINE_ int get_keyed_setters_count() const { return _keyed_setters_count; }
	_FORCE_INLINE_ int get_keyed_getters_count() const { return _keyed_getters_count; }
	_FORCE_INLINE_ int get_indexed_setters_count() const { return _indexed_setters_count; }
	_FORCE_INLINE_ int get_indexed_getters_count() const { return _indexed_getters_count; }
	_FORCE_INLINE_ int get_builtin_methods_count() const { return _builtin_methods_count; }
	_FORCE_INLINE_ int get_constructors_count() const { return _constructors_count; }
	_FORCE_INLINE_ int get_utilities_count() const { return _utilities_count; }
	_FORCE_INLINE_ int get_gds_utilities_count() const { return _gds_utilities_count; }
	_FORCE_INLINE_ int get_methods_count() const { return _methods_count; }
	_FORCE_INLINE_ int get_constants_count() const { return _constant_count; }
	_FORCE_INLINE_ int get_global_names_count() const { return _global_names_count; }
	_FORCE_INLINE_ int get_lambdas_count() const { return _lambdas_count; }
	_FORCE_INLINE_ const Vector<FSFunction *> &get_lambdas() const { return lambdas; }
	_FORCE_INLINE_ const Vector<int> &get_default_argument_offsets() const { return default_arguments; }
	_FORCE_INLINE_ int get_instruction_args_size() const { return _instruction_args_size; }

	// How many of this function's descriptor constants were decoded once because their shape cannot
	// depend on the running frame, and the shape decoded for a given constant (or `nullptr` when that
	// constant is receiver-dependent or is not a tuple descriptor at all).
	_FORCE_INLINE_ int get_predecoded_tuple_shape_count() const { return _predecoded_tuple_shapes.size(); }
	const FSDataType *get_predecoded_tuple_shape_for_constant(int p_constant_index) const;
#endif // TOOLS_ENABLED

	// `p_static_self` describes the exact class handle a static call was made through. The pointed-to
	// descriptor must outlive the call; the frame only borrows it, and copies it into `CallState` if
	// the call suspends.
	Variant call(FSInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_err, CallState *p_state = nullptr, const Variant *p_self_override = nullptr, const FSStaticSelfContext *p_static_self = nullptr);

	// True if the compiled return type or any argument type -- transitively through generic type
	// arguments and typed container element types -- denotes `Self`. A caller that cannot supply a
	// `FSStaticSelfContext` must refuse to invoke a function for which this is true rather than call it
	// with a missing or approximate receiver.
	bool has_self_referencing_signature() const {
		if (return_type.references_self_type()) {
			return true;
		}
		if (rest_parameter_type.references_self_type()) {
			return true;
		}
		for (const FSDataType &argument_type : argument_types) {
			if (argument_type.references_self_type()) {
				return true;
			}
		}
		return false;
	}

	// The cached mirror of `has_self_referencing_signature()` that the call path actually consults. It
	// is only meaningful once the function is fully populated, so it is the value a test must assert on
	// to prove a compiled function is receiver-dependent at call time.
	_FORCE_INLINE_ bool is_receiver_dependent_at_call_time() const { return _references_self_types; }

	// The static receiver descriptor of the innermost script frame executing on this thread, or
	// `nullptr` when the innermost frame is not a static call (or no frame is running). Each frame
	// owns its own descriptor, so nested and concurrent calls never observe each other's.
	static const FSStaticSelfContext *get_current_static_self_context();

	// Dispatches a retroactive-conformance witness on a receiver that has no FSInstance (a native engine
	// object or, later, a builtin value). The witness was compiled against the target's surface, so
	// `self` is bound to `p_self` and member access rides the native/builtin access opcodes; no FS member
	// layout is available, so the witness must not reference instance members.
	Variant call_witness(const Variant &p_self, const Variant **p_args, int p_argcount, Callable::CallError &r_err);
	void debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const;

#ifdef DEBUG_ENABLED
	void _profile_native_call(uint64_t p_t_taken, const String &p_function_name, const String &p_instance_class_name = String());
	void disassemble(const Vector<String> &p_code_lines) const;
#endif

	FSFunction();
	~FSFunction();
};

class FSFunctionState : public ScriptFunctionState {
	FOUNDRY_CLASS(FSFunctionState, ScriptFunctionState);
	friend class FSFunction;
	FSFunction *function = nullptr;
	FSFunction::CallState state;
	Variant _signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Ref<FSFunctionState> first_state;

	// Latches the coroutine's final result once it completes, so that a later `await` of an
	// already-resolved handle resolves immediately instead of parking forever on a `completed`
	// signal that fired exactly once at completion. Only the first state (the one owning the
	// `completed` signal that callers hold as their `Coroutine[T]` handle) ever holds the latch.
	// Both accessors take the `FSLanguage::mutex` so a cross-thread `await` cannot race the latch.
	bool completed_latched = false;
	Variant latched_result;
	void _latch_completed_result(const Variant &p_result);
	bool _try_get_completed_result(Variant &r_result) const;

	SelfList<FSFunctionState> scripts_list;
	SelfList<FSFunctionState> instances_list;

protected:
	static void _bind_methods();

public:
	bool is_valid(bool p_extended_check = false) const;
	Variant resume(const Variant &p_arg = Variant());

#ifdef DEBUG_ENABLED
	// Returns a human-readable representation of the function.
	String get_readable_function() {
		return state.function_name;
	}
#endif

	void _clear_stack();
	void _clear_connections();
	static void abandon_chain(FSFunctionState *p_state);

	FSFunctionState();
	~FSFunctionState();
};
