/**************************************************************************/
/*  fs_byte_codegen.cpp                                                   */
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

#include "fs_byte_codegen.h"

#include "core/debugger/engine_debugger.h"

// Appends every float reachable from `p_value`, in a fixed order, so two constants that are equal
// as Variants can still be told apart by the bits of their floats. Types that cannot hold a float
// contribute nothing and are left to the regular Variant comparison.
static void _collect_constant_floats(const Variant &p_value, LocalVector<double> &r_floats, int p_recursion_count) {
	if (unlikely(p_recursion_count > Variant::MAX_RECURSION_DEPTH)) {
		return;
	}
	const int next_recursion_count = p_recursion_count + 1;

	switch (p_value.get_type()) {
		case Variant::FLOAT: {
			r_floats.push_back(p_value.operator double());
		} break;
		case Variant::VECTOR2: {
			const Vector2 value = p_value;
			r_floats.push_back(value.x);
			r_floats.push_back(value.y);
		} break;
		case Variant::VECTOR3: {
			const Vector3 value = p_value;
			r_floats.push_back(value.x);
			r_floats.push_back(value.y);
			r_floats.push_back(value.z);
		} break;
		case Variant::VECTOR4: {
			const Vector4 value = p_value;
			r_floats.push_back(value.x);
			r_floats.push_back(value.y);
			r_floats.push_back(value.z);
			r_floats.push_back(value.w);
		} break;
		case Variant::RECT2: {
			const Rect2 value = p_value;
			_collect_constant_floats(value.position, r_floats, next_recursion_count);
			_collect_constant_floats(value.size, r_floats, next_recursion_count);
		} break;
		case Variant::TRANSFORM2D: {
			const Transform2D value = p_value;
			for (int i = 0; i < 3; i++) {
				_collect_constant_floats(value.columns[i], r_floats, next_recursion_count);
			}
		} break;
		case Variant::PLANE: {
			const Plane value = p_value;
			_collect_constant_floats(value.normal, r_floats, next_recursion_count);
			r_floats.push_back(value.d);
		} break;
		case Variant::QUATERNION: {
			const Quaternion value = p_value;
			r_floats.push_back(value.x);
			r_floats.push_back(value.y);
			r_floats.push_back(value.z);
			r_floats.push_back(value.w);
		} break;
		case Variant::AABB: {
			const AABB value = p_value;
			_collect_constant_floats(value.position, r_floats, next_recursion_count);
			_collect_constant_floats(value.size, r_floats, next_recursion_count);
		} break;
		case Variant::BASIS: {
			const Basis value = p_value;
			for (int i = 0; i < 3; i++) {
				_collect_constant_floats(value.rows[i], r_floats, next_recursion_count);
			}
		} break;
		case Variant::TRANSFORM3D: {
			const Transform3D value = p_value;
			_collect_constant_floats(value.basis, r_floats, next_recursion_count);
			_collect_constant_floats(value.origin, r_floats, next_recursion_count);
		} break;
		case Variant::PROJECTION: {
			const Projection value = p_value;
			for (int i = 0; i < 4; i++) {
				_collect_constant_floats(value.columns[i], r_floats, next_recursion_count);
			}
		} break;
		case Variant::COLOR: {
			const Color value = p_value;
			r_floats.push_back(value.r);
			r_floats.push_back(value.g);
			r_floats.push_back(value.b);
			r_floats.push_back(value.a);
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			const PackedFloat32Array value = p_value;
			for (const float element : value) {
				r_floats.push_back(element);
			}
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			const PackedFloat64Array value = p_value;
			for (const double element : value) {
				r_floats.push_back(element);
			}
		} break;
		case Variant::PACKED_VECTOR2_ARRAY: {
			const PackedVector2Array value = p_value;
			for (const Vector2 &element : value) {
				_collect_constant_floats(element, r_floats, next_recursion_count);
			}
		} break;
		case Variant::PACKED_VECTOR3_ARRAY: {
			const PackedVector3Array value = p_value;
			for (const Vector3 &element : value) {
				_collect_constant_floats(element, r_floats, next_recursion_count);
			}
		} break;
		case Variant::PACKED_VECTOR4_ARRAY: {
			const PackedVector4Array value = p_value;
			for (const Vector4 &element : value) {
				_collect_constant_floats(element, r_floats, next_recursion_count);
			}
		} break;
		case Variant::PACKED_COLOR_ARRAY: {
			const PackedColorArray value = p_value;
			for (const Color &element : value) {
				_collect_constant_floats(element, r_floats, next_recursion_count);
			}
		} break;
		case Variant::ARRAY: {
			const Array value = p_value;
			for (const Variant &element : value) {
				_collect_constant_floats(element, r_floats, next_recursion_count);
			}
		} break;
		case Variant::DICTIONARY: {
			// Iterated in insertion order on both sides, which is how two dictionaries that compare
			// equal line their entries up.
			const Dictionary value = p_value;
			for (const KeyValue<Variant, Variant> &element : value) {
				_collect_constant_floats(element.key, r_floats, next_recursion_count);
				_collect_constant_floats(element.value, r_floats, next_recursion_count);
			}
		} break;
		default: {
		} break;
	}
}

// The signed/unsigned carrier of every integer the constant reaches, in traversal order. An INT and a
// UINT of equal value compare and hash equally, so without this the pool would merge `1` and `1U` and
// hand the second spelling the first one's carrier.
static void _collect_constant_integer_carriers(const Variant &p_value, LocalVector<Variant::Type> &r_carriers, int p_recursion_count) {
	if (unlikely(p_recursion_count > Variant::MAX_RECURSION_DEPTH)) {
		return;
	}
	const int next_recursion_count = p_recursion_count + 1;

	switch (p_value.get_type()) {
		case Variant::INT:
		case Variant::UINT: {
			r_carriers.push_back(p_value.get_type());
		} break;
		case Variant::ARRAY: {
			const Array value = p_value;
			for (const Variant &element : value) {
				_collect_constant_integer_carriers(element, r_carriers, next_recursion_count);
			}
		} break;
		case Variant::DICTIONARY: {
			// Iterated in insertion order on both sides, which is how two dictionaries that compare
			// equal line their entries up.
			const Dictionary value = p_value;
			for (const KeyValue<Variant, Variant> &element : value) {
				_collect_constant_integer_carriers(element.key, r_carriers, next_recursion_count);
				_collect_constant_integer_carriers(element.value, r_carriers, next_recursion_count);
			}
		} break;
		default: {
		} break;
	}
}

bool FSConstantPoolComparator::compare(const Variant &p_lhs, const Variant &p_rhs) {
	if (!p_lhs.hash_compare(p_rhs)) {
		return false;
	}

	LocalVector<Variant::Type> lhs_carriers;
	LocalVector<Variant::Type> rhs_carriers;
	_collect_constant_integer_carriers(p_lhs, lhs_carriers, 0);
	_collect_constant_integer_carriers(p_rhs, rhs_carriers, 0);
	if (lhs_carriers.size() != rhs_carriers.size()) {
		return false;
	}
	for (uint32_t i = 0; i < lhs_carriers.size(); i++) {
		// Signedness is not erased at runtime, so two constants that differ only in carrier are
		// distinct constants and a script that spells both must get both.
		if (lhs_carriers[i] != rhs_carriers[i]) {
			return false;
		}
	}

	LocalVector<double> lhs_floats;
	LocalVector<double> rhs_floats;
	_collect_constant_floats(p_lhs, lhs_floats, 0);
	_collect_constant_floats(p_rhs, rhs_floats, 0);
	if (lhs_floats.size() != rhs_floats.size()) {
		return false;
	}

	for (uint32_t i = 0; i < lhs_floats.size(); i++) {
		// Compared as bits rather than as values: `0.0 == -0.0` is true, but the two are distinct
		// constants, and a script that spells both must get both.
		if (memcmp(&lhs_floats[i], &rhs_floats[i], sizeof(double)) != 0) {
			return false;
		}
	}
	return true;
}

Variant FSByteCodeGenerator::make_container_type_descriptor(const FSDataType &p_type) const {
	Dictionary descriptor;
	descriptor["builtin_type"] = p_type.builtin_type;
	descriptor["native_type"] = p_type.native_type;
	descriptor["script_type"] = p_type.script_type;
	if (p_type.kind == FSDataType::TUPLE) {
		// A tuple erases to a plain Array, so `builtin_type` alone would read back as a typed array
		// of the first element type. The marker keeps the tuple shape recoverable for `is` tests.
		descriptor["is_tuple"] = true;
	}
	if (p_type.is_nullable) {
		descriptor["is_nullable"] = true;
	}
	if (p_type.is_type_handle) {
		// A `Type[T]` element tests class handles, not instances, so the distinction has to survive
		// the round trip through the descriptor.
		descriptor["is_type_handle"] = true;
	}
	if (p_type.is_self_type) {
		// The class recorded above is what the declaration was lowered against, which for a conformance
		// witness is the conformance target rather than the class the call was made through. The marker
		// is what lets the running frame re-bind this node to its exact receiver; without it the runtime
		// would build metadata for an ancestor specialization.
		descriptor["is_self_type"] = true;
	}
	if (p_type.numeric_type != NumericType::NONE) {
		// Emitted only when a width was declared, so a slot constrained by its carrier alone keeps the
		// exact descriptor shape it had before widths existed. The analyzer builds the container type
		// for the same annotation with the descriptor included, so dropping it here would make the two
		// descriptions of one slot disagree at runtime.
		descriptor["numeric_type"] = int64_t(p_type.numeric_type);
	}

	Array element_types;
	for (const FSDataType &element_type : p_type.container_element_types) {
		element_types.push_back(make_container_type_descriptor(element_type));
	}
	descriptor["element_types"] = element_types;

	if (!p_type.type_arguments.is_empty()) {
		Array type_arguments;
		for (const FSDataType &argument_type : p_type.type_arguments) {
			type_arguments.push_back(make_container_type_descriptor(argument_type));
		}
		descriptor["type_arguments"] = type_arguments;
	}

	return descriptor;
}

int FSByteCodeGenerator::get_container_type_pos(const FSDataType &p_type) {
	// A position that came from `Self` has to reach the runtime as a full descriptor: the bare
	// `script_type` fallback below carries no marker, so the frame could not tell the position apart
	// from one the author spelled out as the class the declaration was lowered against.
	if (p_type.references_self_type()) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	if ((p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY) && p_type.has_container_element_types()) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	// A specialized object element type (e.g. `Box[int]` as the element of `Array[Box[int]]`) carries
	// reified type arguments that a bare `script_type` constant would drop. Emit the full descriptor so
	// the runtime element metadata keeps `Box[int]` and `Box[String]` distinguishable.
	if (!p_type.type_arguments.is_empty()) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	// A `Type[T]` element tests class handles rather than instances. A bare `script_type` constant
	// cannot express that, and a native handle has no script at all, so the full descriptor is the
	// only encoding that keeps `Array[Type[Node]]` distinct from `Array[Node]` at runtime.
	if (p_type.is_type_handle) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	// The bare `script_type` fallback below transports only the operand-encoded carrier, which cannot
	// tell a 32-bit constraint from a 64-bit one. A slot that declared a width therefore needs the full
	// descriptor, or `Array[ulong]` would reach the runtime as the carrier-named, unconstrained
	// `Array[uint]` and disagree with the analyzer-built container for the same annotation.
	if (p_type.numeric_type != NumericType::NONE) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	return get_constant_pos(p_type.script_type);
}

int FSByteCodeGenerator::get_native_type_pos(const FSDataType &p_type) {
	// A position that came from `Self` has to reach the runtime as a full descriptor. The
	// `FSNativeClass` constant below names only the engine class the declaration was lowered against,
	// which for a native conformance witness is the conformance target rather than the class the call
	// was made through, so the frame could not re-bind it to its exact receiver.
	if (p_type.references_self_type()) {
		return get_constant_pos(make_container_type_descriptor(p_type));
	}
	const int class_index = FSLanguage::get_singleton()->get_global_map()[p_type.native_type];
	return get_constant_pos(FSLanguage::get_singleton()->get_global_array()[class_index]);
}

uint32_t FSByteCodeGenerator::add_parameter(const StringName &p_name, bool p_is_optional, const FSDataType &p_type) {
	function->_argument_count++;
	function->argument_types.push_back(p_type);
	if (p_is_optional) {
		function->_default_arg_count++;
	}

	return add_local(p_name, p_type);
}

uint32_t FSByteCodeGenerator::add_local(const StringName &p_name, const FSDataType &p_type) {
	int stack_pos = locals.size() + FSFunction::FIXED_ADDRESSES_MAX;
	locals.push_back(StackSlot(p_type.builtin_type, p_type.can_contain_object()));
	add_stack_identifier(p_name, stack_pos);
	return stack_pos;
}

uint32_t FSByteCodeGenerator::add_local_constant(const StringName &p_name, const Variant &p_constant) {
	int index = add_or_get_constant(p_constant);
	local_constants[p_name] = index;
	return index;
}

uint32_t FSByteCodeGenerator::add_or_get_constant(const Variant &p_constant) {
	return get_constant_pos(p_constant);
}

uint32_t FSByteCodeGenerator::add_or_get_name(const StringName &p_name) {
	return get_name_map_pos(p_name);
}

uint32_t FSByteCodeGenerator::add_temporary(const FSDataType &p_type) {
	Variant::Type temp_type = Variant::NIL;
	if (p_type.kind == FSDataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::NIL:
			case Variant::BOOL:
			case Variant::INT:
			case Variant::FLOAT:
			case Variant::STRING:
			case Variant::VECTOR2:
			case Variant::VECTOR2I:
			case Variant::RECT2:
			case Variant::RECT2I:
			case Variant::VECTOR3:
			case Variant::VECTOR3I:
			case Variant::TRANSFORM2D:
			case Variant::VECTOR4:
			case Variant::VECTOR4I:
			case Variant::PLANE:
			case Variant::QUATERNION:
			case Variant::AABB:
			case Variant::BASIS:
			case Variant::TRANSFORM3D:
			case Variant::PROJECTION:
			case Variant::COLOR:
			case Variant::STRING_NAME:
			case Variant::NODE_PATH:
			case Variant::RID:
			case Variant::CALLABLE:
			case Variant::SIGNAL:
				temp_type = p_type.builtin_type;
				break;
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
			case Variant::UINT:
			case Variant::VARIANT_MAX:
				// Arrays, dictionaries, and objects are reference counted, so we don't use the pool for them.
				temp_type = Variant::NIL;
				break;
		}
	}

	if (!temporaries_pool.has(temp_type)) {
		temporaries_pool[temp_type] = List<int>();
	}

	List<int> &pool = temporaries_pool[temp_type];
	if (pool.is_empty()) {
		StackSlot new_temp(temp_type, p_type.can_contain_object());
		int idx = temporaries.size();
		pool.push_back(idx);
		temporaries.push_back(new_temp);
	}
	int slot = pool.front()->get();
	pool.pop_front();
	used_temporaries.push_back(slot);
	return slot;
}

void FSByteCodeGenerator::pop_temporary() {
	ERR_FAIL_COND(used_temporaries.is_empty());
	int slot_idx = used_temporaries.back()->get();
	if (temporaries[slot_idx].can_contain_object) {
		// Avoid keeping in the stack long-lived references to objects,
		// which may prevent `RefCounted` objects from being freed.
		// However, the cleanup will be performed an the end of the
		// statement, to allow object references to survive chaining.
		temporaries_pending_clear.insert(slot_idx);
	}
	temporaries_pool[temporaries[slot_idx].type].push_back(slot_idx);
	used_temporaries.pop_back();
}

void FSByteCodeGenerator::start_parameters() {
	if (function->_default_arg_count > 0) {
		append(FSFunction::OPCODE_JUMP_TO_DEF_ARGUMENT);
		function->default_arguments.push_back(opcodes.size());
	}
}

void FSByteCodeGenerator::end_parameters() {
	function->default_arguments.reverse();
}

void FSByteCodeGenerator::write_start(FoundryScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const FSDataType &p_return_type) {
	function = memnew(FSFunction);

	function->name = p_function_name;
	function->_script = p_script;
	function->source = p_script->get_script_path();

#ifdef DEBUG_ENABLED
	function->func_cname = (String(function->source) + " - " + String(p_function_name)).utf8();
	function->_func_cname = function->func_cname.get_data();
#endif

	function->_static = p_static;
	function->return_type = p_return_type;
	function->rpc_config = p_rpc_config;
	function->_argument_count = 0;
}

FSFunction *FSByteCodeGenerator::write_end() {
#ifdef DEBUG_ENABLED
	if (!used_temporaries.is_empty()) {
		ERR_PRINT("Non-zero temporary variables at end of function: " + itos(used_temporaries.size()));
	}
#endif
	append_opcode(FSFunction::OPCODE_END);

	for (int i = 0; i < temporaries.size(); i++) {
		int stack_index = i + max_locals + FSFunction::FIXED_ADDRESSES_MAX;
		for (int j = 0; j < temporaries[i].bytecode_indices.size(); j++) {
			opcodes.write[temporaries[i].bytecode_indices[j]] = stack_index | (FSFunction::ADDR_TYPE_STACK << FSFunction::ADDR_BITS);
		}
		if (temporaries[i].type != Variant::NIL) {
			function->temporary_slots[stack_index] = temporaries[i].type;
		}
	}

	function->constants.resize(constant_map.size());
	for (const KeyValue<Variant, int> &K : constant_map) {
		function->constants.write[K.value] = K.key;
	}

	function->global_names.resize(name_map.size());
	for (const KeyValue<StringName, int> &E : name_map) {
		function->global_names.write[E.value] = E.key;
	}

	function->code = opcodes;

	function->operator_funcs.resize(operator_func_map.size());
	for (const KeyValue<Variant::ValidatedOperatorEvaluator, int> &E : operator_func_map) {
		function->operator_funcs.write[E.value] = E.key;
	}

	function->setters.resize(setters_map.size());
	for (const KeyValue<Variant::ValidatedSetter, int> &E : setters_map) {
		function->setters.write[E.value] = E.key;
	}

	function->getters.resize(getters_map.size());
	for (const KeyValue<Variant::ValidatedGetter, int> &E : getters_map) {
		function->getters.write[E.value] = E.key;
	}

	function->keyed_setters.resize(keyed_setters_map.size());
	for (const KeyValue<Variant::ValidatedKeyedSetter, int> &E : keyed_setters_map) {
		function->keyed_setters.write[E.value] = E.key;
	}

	function->keyed_getters.resize(keyed_getters_map.size());
	for (const KeyValue<Variant::ValidatedKeyedGetter, int> &E : keyed_getters_map) {
		function->keyed_getters.write[E.value] = E.key;
	}

	function->indexed_setters.resize(indexed_setters_map.size());
	for (const KeyValue<Variant::ValidatedIndexedSetter, int> &E : indexed_setters_map) {
		function->indexed_setters.write[E.value] = E.key;
	}

	function->indexed_getters.resize(indexed_getters_map.size());
	for (const KeyValue<Variant::ValidatedIndexedGetter, int> &E : indexed_getters_map) {
		function->indexed_getters.write[E.value] = E.key;
	}

	function->builtin_methods.resize(builtin_method_map.size());
	for (const KeyValue<Variant::ValidatedBuiltInMethod, int> &E : builtin_method_map) {
		function->builtin_methods.write[E.value] = E.key;
	}
	function->builtin_method_names = builtin_method_names;

	function->constructors.resize(constructors_map.size());
	for (const KeyValue<Variant::ValidatedConstructor, int> &E : constructors_map) {
		function->constructors.write[E.value] = E.key;
	}

	function->utilities.resize(utilities_map.size());
	for (const KeyValue<Variant::ValidatedUtilityFunction, int> &E : utilities_map) {
		function->utilities.write[E.value] = E.key;
	}

	function->gds_utilities.resize(gds_utilities_map.size());
	for (const KeyValue<FSUtilityFunctions::FunctionPtr, int> &E : gds_utilities_map) {
		function->gds_utilities.write[E.value] = E.key;
	}

	function->methods.resize(method_bind_map.size());
	for (const KeyValue<MethodBind *, int> &E : method_bind_map) {
		function->methods.write[E.value] = E.key;
	}

	function->lambdas.resize(lambdas_map.size());
	for (const KeyValue<FSFunction *, int> &E : lambdas_map) {
		function->lambdas.write[E.value] = E.key;
	}

	if (FSLanguage::get_singleton()->should_track_locals()) {
		function->stack_debug = stack_debug;
	}
	function->_stack_size = FSFunction::FIXED_ADDRESSES_MAX + max_locals + temporaries.size();
	function->_instruction_args_size = instr_args_max;

	function->setup_runtime_pointers();

#ifdef DEBUG_ENABLED
	function->operator_names = operator_names;
	function->setter_names = setter_names;
	function->getter_names = getter_names;
	function->builtin_methods_names = builtin_methods_names;
	function->constructors_names = constructors_names;
	function->utilities_names = utilities_names;
	function->gds_utilities_names = gds_utilities_names;
#endif

#ifdef TOOLS_ENABLED
	DEV_ASSERT(export_fixups.operators.size() == operator_func_map.size());
	DEV_ASSERT(export_fixups.setters.size() == setters_map.size());
	DEV_ASSERT(export_fixups.getters.size() == getters_map.size());
	DEV_ASSERT(export_fixups.keyed_setters.size() == keyed_setters_map.size());
	DEV_ASSERT(export_fixups.keyed_getters.size() == keyed_getters_map.size());
	DEV_ASSERT(export_fixups.indexed_setters.size() == indexed_setters_map.size());
	DEV_ASSERT(export_fixups.indexed_getters.size() == indexed_getters_map.size());
	DEV_ASSERT(export_fixups.builtin_methods.size() == builtin_method_map.size());
	DEV_ASSERT(export_fixups.constructors.size() == constructors_map.size());
	DEV_ASSERT(export_fixups.utilities.size() == utilities_map.size());
	DEV_ASSERT(export_fixups.gds_utilities.size() == gds_utilities_map.size());
	DEV_ASSERT(export_fixups.method_binds.size() == method_bind_map.size());
	export_fixups.named_globals = named_globals;
	function->export_fixups = export_fixups;
#endif

	ended = true;
	return function;
}

#ifdef DEBUG_ENABLED
void FSByteCodeGenerator::set_signature(const String &p_signature) {
	function->profile.signature = p_signature;
}
#endif

void FSByteCodeGenerator::set_initial_line(int p_line) {
	function->_initial_line = p_line;
}

// Both macros gate the validated fast paths, which bake the declared builtin type into the emitted
// instruction and never inspect the value at runtime. A nullable slot can legitimately hold null
// (`OPCODE_ASSIGN_TYPED_BUILTIN` stores it as-is), so it must fall back to the generic opcodes that
// dispatch on the actual runtime type. Otherwise `String? != null` would answer from the validated
// `(STRING, NIL)` evaluator — a constant `true` — and a null receiver would be read as if it held
// the underlying type.
#define HAS_BUILTIN_TYPE(m_var) \
	(m_var.type.kind == FSDataType::BUILTIN && !m_var.type.is_nullable)

#define IS_BUILTIN_TYPE(m_var, m_type) \
	(m_var.type.kind == FSDataType::BUILTIN && !m_var.type.is_nullable && m_var.type.builtin_type == m_type && m_type != Variant::NIL)

void FSByteCodeGenerator::write_type_adjust(const Address &p_target, Variant::Type p_new_type) {
	switch (p_new_type) {
		case Variant::BOOL:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_BOOL);
			break;
		case Variant::INT:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_INT);
			break;
		case Variant::FLOAT:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_FLOAT);
			break;
		case Variant::STRING:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_STRING);
			break;
		case Variant::VECTOR2:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR2);
			break;
		case Variant::VECTOR2I:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR2I);
			break;
		case Variant::RECT2:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_RECT2);
			break;
		case Variant::RECT2I:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_RECT2I);
			break;
		case Variant::VECTOR3:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR3);
			break;
		case Variant::VECTOR3I:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR3I);
			break;
		case Variant::TRANSFORM2D:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_TRANSFORM2D);
			break;
		case Variant::VECTOR4:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR3);
			break;
		case Variant::VECTOR4I:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_VECTOR3I);
			break;
		case Variant::PLANE:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PLANE);
			break;
		case Variant::QUATERNION:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_QUATERNION);
			break;
		case Variant::AABB:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_AABB);
			break;
		case Variant::BASIS:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_BASIS);
			break;
		case Variant::TRANSFORM3D:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_TRANSFORM3D);
			break;
		case Variant::PROJECTION:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PROJECTION);
			break;
		case Variant::COLOR:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_COLOR);
			break;
		case Variant::STRING_NAME:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_STRING_NAME);
			break;
		case Variant::NODE_PATH:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_NODE_PATH);
			break;
		case Variant::RID:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_RID);
			break;
		case Variant::OBJECT:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_OBJECT);
			break;
		case Variant::CALLABLE:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_CALLABLE);
			break;
		case Variant::SIGNAL:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_SIGNAL);
			break;
		case Variant::DICTIONARY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_DICTIONARY);
			break;
		case Variant::ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_ARRAY);
			break;
		case Variant::PACKED_BYTE_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY);
			break;
		case Variant::PACKED_INT32_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY);
			break;
		case Variant::PACKED_INT64_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY);
			break;
		case Variant::PACKED_FLOAT32_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY);
			break;
		case Variant::PACKED_FLOAT64_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY);
			break;
		case Variant::PACKED_STRING_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY);
			break;
		case Variant::PACKED_VECTOR2_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY);
			break;
		case Variant::PACKED_VECTOR3_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY);
			break;
		case Variant::PACKED_COLOR_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY);
			break;
		case Variant::PACKED_VECTOR4_ARRAY:
			append_opcode(FSFunction::OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY);
			break;
		case Variant::NIL:
		case Variant::UINT:
		case Variant::VARIANT_MAX:
			return;
	}
	append(p_target);
}

void FSByteCodeGenerator::write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) {
	if (HAS_BUILTIN_TYPE(p_left_operand)) {
		// Gather specific operator.
		Variant::ValidatedOperatorEvaluator op_func = Variant::get_validated_operator_evaluator(p_operator, p_left_operand.type.builtin_type, Variant::NIL);

		append_opcode(FSFunction::OPCODE_OPERATOR_VALIDATED);
		append(p_left_operand);
		append(Address());
		append(p_target);
		append(op_func);
#ifdef TOOLS_ENABLED
		record_export_fixup(export_fixups.operators, get_operation_pos(op_func), FSFunction::ExportFixups::OperatorKey{ p_operator, p_left_operand.type.builtin_type, Variant::NIL });
#endif
#ifdef DEBUG_ENABLED
		add_debug_name(operator_names, get_operation_pos(op_func), Variant::get_operator_name(p_operator));
#endif
		return;
	}

	// No specific types, perform variant evaluation.
#ifdef TOOLS_ENABLED
	const int unary_operator_offset = opcodes.size();
#endif
	append_opcode(FSFunction::OPCODE_OPERATOR);
	append(p_left_operand);
	append(Address());
	append(p_target);
	append(p_operator);
	append(0); // Signature storage.
	append(0); // Return type storage.
	constexpr int _pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(*(opcodes.ptr()));
	for (int i = 0; i < _pointer_size; i++) {
		append(0); // Space for function pointer.
	}
#ifdef TOOLS_ENABLED
	export_fixups.operator_cache_offsets.push_back(unary_operator_offset);
#endif
}

void FSByteCodeGenerator::write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) {
	bool valid = HAS_BUILTIN_TYPE(p_left_operand) && HAS_BUILTIN_TYPE(p_right_operand);

	// Avoid validated evaluator for modulo and division when operands are int or integer vector, since there's no check for division by zero.
	if (valid && (p_operator == Variant::OP_DIVIDE || p_operator == Variant::OP_MODULE)) {
		switch (p_left_operand.type.builtin_type) {
			case Variant::INT:
				// Cannot use modulo between int / float, we should raise an error later in FoundryScript
				valid = p_right_operand.type.builtin_type != Variant::INT && p_operator == Variant::OP_DIVIDE;
				break;
			case Variant::VECTOR2I:
			case Variant::VECTOR3I:
			case Variant::VECTOR4I:
				valid = p_right_operand.type.builtin_type != Variant::INT && p_right_operand.type.builtin_type != p_left_operand.type.builtin_type;
				break;
			default:
				break;
		}
	}

	if (valid) {
		if (p_target.mode == Address::TEMPORARY) {
			Variant::Type result_type = Variant::get_operator_return_type(p_operator, p_left_operand.type.builtin_type, p_right_operand.type.builtin_type);
			Variant::Type temp_type = temporaries[p_target.address].type;
			if (result_type != temp_type) {
				write_type_adjust(p_target, result_type);
			}
		}

		// Gather specific operator.
		Variant::ValidatedOperatorEvaluator op_func = Variant::get_validated_operator_evaluator(p_operator, p_left_operand.type.builtin_type, p_right_operand.type.builtin_type);

		append_opcode(FSFunction::OPCODE_OPERATOR_VALIDATED);
		append(p_left_operand);
		append(p_right_operand);
		append(p_target);
		append(op_func);
#ifdef TOOLS_ENABLED
		record_export_fixup(export_fixups.operators, get_operation_pos(op_func), FSFunction::ExportFixups::OperatorKey{ p_operator, p_left_operand.type.builtin_type, p_right_operand.type.builtin_type });
#endif
#ifdef DEBUG_ENABLED
		add_debug_name(operator_names, get_operation_pos(op_func), Variant::get_operator_name(p_operator));
#endif
		return;
	}

	// No specific types, perform variant evaluation.
#ifdef TOOLS_ENABLED
	const int binary_operator_offset = opcodes.size();
#endif
	append_opcode(FSFunction::OPCODE_OPERATOR);
	append(p_left_operand);
	append(p_right_operand);
	append(p_target);
	append(p_operator);
	append(0); // Signature storage.
	append(0); // Return type storage.
	constexpr int _pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(*(opcodes.ptr()));
	for (int i = 0; i < _pointer_size; i++) {
		append(0); // Space for function pointer.
	}
#ifdef TOOLS_ENABLED
	export_fixups.operator_cache_offsets.push_back(binary_operator_offset);
#endif
}

void FSByteCodeGenerator::write_type_test(const Address &p_target, const Address &p_source, const FSDataType &p_type) {
	switch (p_type.kind) {
		case FSDataType::BUILTIN: {
			if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
				const FSDataType &element_type = p_type.get_container_element_type(0);
				append_opcode(FSFunction::OPCODE_TYPE_TEST_ARRAY);
				append(p_target);
				append(p_source);
				append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(element_type.builtin_type);
				append(element_type.native_type);
			} else if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
				const FSDataType &key_element_type = p_type.get_container_element_type_or_variant(0);
				const FSDataType &value_element_type = p_type.get_container_element_type_or_variant(1);
				append_opcode(FSFunction::OPCODE_TYPE_TEST_DICTIONARY);
				append(p_target);
				append(p_source);
				append(get_container_type_pos(key_element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(get_container_type_pos(value_element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(key_element_type.builtin_type);
				append(key_element_type.native_type);
				append(value_element_type.builtin_type);
				append(value_element_type.native_type);
			} else {
				append_opcode(FSFunction::OPCODE_TYPE_TEST_BUILTIN);
				append(p_target);
				append(p_source);
				append(p_type.builtin_type | (p_type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0));
			}
		} break;
		case FSDataType::TUPLE: {
			append_opcode(FSFunction::OPCODE_TYPE_TEST_TUPLE);
			append(p_target);
			append(p_source);
			append(get_container_type_pos(p_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
			append(p_type.container_element_types.size());
		} break;
		case FSDataType::NATIVE: {
			append_opcode(FSFunction::OPCODE_TYPE_TEST_NATIVE);
			append(p_target);
			append(p_source);
			append(get_native_type_pos(p_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
			append(p_type.is_type_handle);
		} break;
		case FSDataType::SCRIPT:
		case FSDataType::FOUNDRY_SCRIPT: {
			const Variant &script = p_type.script_type;
			append_opcode(FSFunction::OPCODE_TYPE_TEST_SCRIPT);
			append(p_target);
			append(p_source);
			// A `Self` position is tested against the frame's exact receiver, so it travels as a
			// descriptor; the bare script constant would test against the class the declaration was
			// lowered against and answer for an ancestor specialization.
			const bool needs_descriptor = p_type.references_self_type() || (p_type.is_type_handle && !p_type.type_arguments.is_empty());
			const int type_idx = needs_descriptor ? get_container_type_pos(p_type) : get_constant_pos(script);
			append(type_idx | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
			append(p_type.is_type_handle);
		} break;
		default: {
			ERR_PRINT("Compiler bug: unresolved type in type test.");
			append_opcode(FSFunction::OPCODE_ASSIGN_FALSE);
			append(p_target);
		}
	}
}

void FSByteCodeGenerator::write_type_test_enum(const Address &p_target, const Address &p_source, const PackedInt64Array &p_declared_values, bool p_is_tagged_union) {
	append_opcode(FSFunction::OPCODE_TYPE_TEST_ENUM);
	append(p_target);
	append(p_source);
	append(get_constant_pos(p_declared_values) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(p_is_tagged_union ? 1 : 0);
}

void FSByteCodeGenerator::write_type_test_enum_case(const Address &p_target, const Address &p_source, int p_tag, const Vector<Address> &p_binds) {
	append_opcode_and_argcount(FSFunction::OPCODE_TYPE_TEST_ENUM_CASE, p_binds.size() + 2);
	for (const Address &bind : p_binds) {
		append(bind);
	}
	append(p_source);
	append(p_target);
	append(p_tag);
	append(p_binds.size());
}

void FSByteCodeGenerator::write_and_left_operand(const Address &p_left_operand) {
	append_opcode(FSFunction::OPCODE_JUMP_IF_NOT);
	append(p_left_operand);
	logic_op_jump_pos1.push_back(opcodes.size());
	append(0); // Jump target, will be patched.
}

void FSByteCodeGenerator::write_and_right_operand(const Address &p_right_operand) {
	append_opcode(FSFunction::OPCODE_JUMP_IF_NOT);
	append(p_right_operand);
	logic_op_jump_pos2.push_back(opcodes.size());
	append(0); // Jump target, will be patched.
}

void FSByteCodeGenerator::write_end_and(const Address &p_target) {
	// If here means both operands are true.
	append_opcode(FSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
	// Jump away from the fail condition.
	append_opcode(FSFunction::OPCODE_JUMP);
	append(opcodes.size() + 3);
	// Here it means one of operands is false.
	patch_jump(logic_op_jump_pos1.back()->get());
	patch_jump(logic_op_jump_pos2.back()->get());
	logic_op_jump_pos1.pop_back();
	logic_op_jump_pos2.pop_back();
	append_opcode(FSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
}

void FSByteCodeGenerator::write_or_left_operand(const Address &p_left_operand) {
	append_opcode(FSFunction::OPCODE_JUMP_IF);
	append(p_left_operand);
	logic_op_jump_pos1.push_back(opcodes.size());
	append(0); // Jump target, will be patched.
}

void FSByteCodeGenerator::write_or_right_operand(const Address &p_right_operand) {
	append_opcode(FSFunction::OPCODE_JUMP_IF);
	append(p_right_operand);
	logic_op_jump_pos2.push_back(opcodes.size());
	append(0); // Jump target, will be patched.
}

void FSByteCodeGenerator::write_end_or(const Address &p_target) {
	// If here means both operands are false.
	append_opcode(FSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
	// Jump away from the success condition.
	append_opcode(FSFunction::OPCODE_JUMP);
	append(opcodes.size() + 3);
	// Here it means one of operands is true.
	patch_jump(logic_op_jump_pos1.back()->get());
	patch_jump(logic_op_jump_pos2.back()->get());
	logic_op_jump_pos1.pop_back();
	logic_op_jump_pos2.pop_back();
	append_opcode(FSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
}

void FSByteCodeGenerator::write_start_ternary(const Address &p_target) {
	ternary_result.push_back(p_target);
}

void FSByteCodeGenerator::write_ternary_condition(const Address &p_condition) {
	append_opcode(FSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	ternary_jump_fail_pos.push_back(opcodes.size());
	append(0); // Jump target, will be patched.
}

void FSByteCodeGenerator::write_ternary_true_expr(const Address &p_expr) {
	append_opcode(FSFunction::OPCODE_ASSIGN);
	append(ternary_result.back()->get());
	append(p_expr);
	// Jump away from the false path.
	append_opcode(FSFunction::OPCODE_JUMP);
	ternary_jump_skip_pos.push_back(opcodes.size());
	append(0);
	// Fail must jump here.
	patch_jump(ternary_jump_fail_pos.back()->get());
	ternary_jump_fail_pos.pop_back();
}

void FSByteCodeGenerator::write_ternary_false_expr(const Address &p_expr) {
	append_opcode(FSFunction::OPCODE_ASSIGN);
	append(ternary_result.back()->get());
	append(p_expr);
}

void FSByteCodeGenerator::write_end_ternary() {
	patch_jump(ternary_jump_skip_pos.back()->get());
	ternary_jump_skip_pos.pop_back();
	ternary_result.pop_back();
}

void FSByteCodeGenerator::write_set(const Address &p_target, const Address &p_index, const Address &p_source) {
	if (HAS_BUILTIN_TYPE(p_target)) {
		if (IS_BUILTIN_TYPE(p_index, Variant::INT) && Variant::get_member_validated_indexed_setter(p_target.type.builtin_type) &&
				IS_BUILTIN_TYPE(p_source, Variant::get_indexed_element_type(p_target.type.builtin_type))) {
			// Use indexed setter instead.
			Variant::ValidatedIndexedSetter setter = Variant::get_member_validated_indexed_setter(p_target.type.builtin_type);
			append_opcode(FSFunction::OPCODE_SET_INDEXED_VALIDATED);
			append(p_target);
			append(p_index);
			append(p_source);
			append(setter);
#ifdef TOOLS_ENABLED
			record_export_fixup(export_fixups.indexed_setters, get_indexed_setter_pos(setter), p_target.type.builtin_type);
#endif
			return;
		} else if (Variant::get_member_validated_keyed_setter(p_target.type.builtin_type)) {
			Variant::ValidatedKeyedSetter setter = Variant::get_member_validated_keyed_setter(p_target.type.builtin_type);
			append_opcode(FSFunction::OPCODE_SET_KEYED_VALIDATED);
			append(p_target);
			append(p_index);
			append(p_source);
			append(setter);
#ifdef TOOLS_ENABLED
			record_export_fixup(export_fixups.keyed_setters, get_keyed_setter_pos(setter), p_target.type.builtin_type);
#endif
			return;
		}
	}

	append_opcode(FSFunction::OPCODE_SET_KEYED);
	append(p_target);
	append(p_index);
	append(p_source);
}

void FSByteCodeGenerator::write_get(const Address &p_target, const Address &p_index, const Address &p_source) {
	if (HAS_BUILTIN_TYPE(p_source)) {
		if (IS_BUILTIN_TYPE(p_index, Variant::INT) && Variant::get_member_validated_indexed_getter(p_source.type.builtin_type)) {
			// Use indexed getter instead.
			Variant::ValidatedIndexedGetter getter = Variant::get_member_validated_indexed_getter(p_source.type.builtin_type);
			append_opcode(FSFunction::OPCODE_GET_INDEXED_VALIDATED);
			append(p_source);
			append(p_index);
			append(p_target);
			append(getter);
#ifdef TOOLS_ENABLED
			record_export_fixup(export_fixups.indexed_getters, get_indexed_getter_pos(getter), p_source.type.builtin_type);
#endif
			return;
		} else if (Variant::get_member_validated_keyed_getter(p_source.type.builtin_type)) {
			Variant::ValidatedKeyedGetter getter = Variant::get_member_validated_keyed_getter(p_source.type.builtin_type);
			append_opcode(FSFunction::OPCODE_GET_KEYED_VALIDATED);
			append(p_source);
			append(p_index);
			append(p_target);
			append(getter);
#ifdef TOOLS_ENABLED
			record_export_fixup(export_fixups.keyed_getters, get_keyed_getter_pos(getter), p_source.type.builtin_type);
#endif
			return;
		}
	}
	append_opcode(FSFunction::OPCODE_GET_KEYED);
	append(p_source);
	append(p_index);
	append(p_target);
}

void FSByteCodeGenerator::write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	if (HAS_BUILTIN_TYPE(p_target) && Variant::get_member_validated_setter(p_target.type.builtin_type, p_name) &&
			IS_BUILTIN_TYPE(p_source, Variant::get_member_type(p_target.type.builtin_type, p_name))) {
		Variant::ValidatedSetter setter = Variant::get_member_validated_setter(p_target.type.builtin_type, p_name);
		append_opcode(FSFunction::OPCODE_SET_NAMED_VALIDATED);
		append(p_target);
		append(p_source);
		append(setter);
#ifdef TOOLS_ENABLED
		record_export_fixup(export_fixups.setters, get_setter_pos(setter), FSFunction::ExportFixups::TypedNameKey{ p_target.type.builtin_type, p_name });
#endif
#ifdef DEBUG_ENABLED
		add_debug_name(setter_names, get_setter_pos(setter), p_name);
#endif
		return;
	}
	append_opcode(FSFunction::OPCODE_SET_NAMED);
	append(p_target);
	append(p_source);
	append(p_name);
}

void FSByteCodeGenerator::write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	if (HAS_BUILTIN_TYPE(p_source) && Variant::get_member_validated_getter(p_source.type.builtin_type, p_name)) {
		Variant::ValidatedGetter getter = Variant::get_member_validated_getter(p_source.type.builtin_type, p_name);
		append_opcode(FSFunction::OPCODE_GET_NAMED_VALIDATED);
		append(p_source);
		append(p_target);
		append(getter);
#ifdef TOOLS_ENABLED
		record_export_fixup(export_fixups.getters, get_getter_pos(getter), FSFunction::ExportFixups::TypedNameKey{ p_source.type.builtin_type, p_name });
#endif
#ifdef DEBUG_ENABLED
		add_debug_name(getter_names, get_getter_pos(getter), p_name);
#endif
		return;
	}
	append_opcode(FSFunction::OPCODE_GET_NAMED);
	append(p_source);
	append(p_target);
	append(p_name);
}

void FSByteCodeGenerator::write_set_member(const Address &p_value, const StringName &p_name) {
	append_opcode(FSFunction::OPCODE_SET_MEMBER);
	append(p_value);
	append(p_name);
}

void FSByteCodeGenerator::write_get_member(const Address &p_target, const StringName &p_name) {
	append_opcode(FSFunction::OPCODE_GET_MEMBER);
	append(p_target);
	append(p_name);
}

void FSByteCodeGenerator::write_get_type_parameter(const Address &p_target, int p_type_parameter_index) {
	append_opcode(FSFunction::OPCODE_GET_TYPE_PARAMETER);
	append(p_target);
	append(p_type_parameter_index);
}

void FSByteCodeGenerator::write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) {
	append_opcode(FSFunction::OPCODE_SET_STATIC_VARIABLE);
	append(p_value);
	append(p_class);
	append(p_index);
}

void FSByteCodeGenerator::write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) {
	append_opcode(FSFunction::OPCODE_GET_STATIC_VARIABLE);
	append(p_target);
	append(p_class);
	append(p_index);
}

void FSByteCodeGenerator::write_assign_with_conversion(const Address &p_target, const Address &p_source) {
	switch (p_target.type.kind) {
		case FSDataType::BUILTIN: {
			if (p_target.type.builtin_type == Variant::ARRAY && p_target.type.has_container_element_type(0)) {
				const FSDataType &element_type = p_target.type.get_container_element_type(0);
				append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_ARRAY);
				append(p_target);
				append(p_source);
				append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(element_type.builtin_type);
				append(element_type.native_type);
			} else if (p_target.type.builtin_type == Variant::DICTIONARY && p_target.type.has_container_element_types()) {
				const FSDataType &key_type = p_target.type.get_container_element_type_or_variant(0);
				const FSDataType &value_type = p_target.type.get_container_element_type_or_variant(1);
				append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY);
				append(p_target);
				append(p_source);
				append(get_container_type_pos(key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(get_container_type_pos(value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(key_type.builtin_type);
				append(key_type.native_type);
				append(value_type.builtin_type);
				append(value_type.native_type);
			} else {
				append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_BUILTIN);
				append(p_target);
				append(p_source);
				append(p_target.type.builtin_type | (p_target.type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0));
			}
		} break;
		case FSDataType::NATIVE: {
			int class_idx = FSLanguage::get_singleton()->get_global_map()[p_target.type.native_type];
			Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
			class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
			append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_NATIVE);
			append(p_target);
			append(p_source);
			append(class_idx);
			append(p_target.type.is_type_handle);
		} break;
		case FSDataType::SCRIPT:
		case FSDataType::FOUNDRY_SCRIPT: {
			Variant script = p_target.type.script_type;
			int idx = p_target.type.is_type_handle && !p_target.type.type_arguments.is_empty() ? get_container_type_pos(p_target.type) : get_constant_pos(script);
			idx |= (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);

			append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_SCRIPT);
			append(p_target);
			append(p_source);
			append(idx);
			append(p_target.type.is_type_handle);
		} break;
		case FSDataType::VARIANT: {
			// Converting into an untyped slot (including a generic parameter erased to Variant) is an
			// identity assignment, so emit a plain assign instead of the bug-catcher below.
			append_opcode(FSFunction::OPCODE_ASSIGN);
			append(p_target);
			append(p_source);
		} break;
		default: {
			ERR_PRINT("Compiler bug: unresolved assign.");

			// Shouldn't get here, but fail-safe to a regular assignment
			append_opcode(FSFunction::OPCODE_ASSIGN);
			append(p_target);
			append(p_source);
		}
	}
}

void FSByteCodeGenerator::write_assign(const Address &p_target, const Address &p_source) {
	if (p_target.type.kind == FSDataType::NATIVE &&
			ClassDB::is_parent_class(SNAME("FoundryScript"), p_target.type.native_type) &&
			p_source.type.is_type_handle && !p_source.type.type_arguments.is_empty()) {
		int class_idx = FSLanguage::get_singleton()->get_global_map()[p_target.type.native_type];
		Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
		class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
		append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_NATIVE);
		append(p_target);
		append(p_source);
		append(class_idx);
		append(false);
		return;
	}

	if (p_target.type.is_type_handle) {
		switch (p_target.type.kind) {
			case FSDataType::NATIVE: {
				int class_idx = FSLanguage::get_singleton()->get_global_map()[p_target.type.native_type];
				Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
				class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
				append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_NATIVE);
				append(p_target);
				append(p_source);
				append(class_idx);
				append(true);
				return;
			}
			case FSDataType::SCRIPT:
			case FSDataType::FOUNDRY_SCRIPT: {
				Variant script = p_target.type.script_type;
				int idx = p_target.type.is_type_handle && !p_target.type.type_arguments.is_empty() ? get_container_type_pos(p_target.type) : get_constant_pos(script);
				idx |= (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
				append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_SCRIPT);
				append(p_target);
				append(p_source);
				append(idx);
				append(true);
				return;
			}
			default:
				break;
		}
	}

	if (p_target.type.kind == FSDataType::BUILTIN && p_target.type.builtin_type == Variant::ARRAY && p_target.type.has_container_element_type(0)) {
		const FSDataType &element_type = p_target.type.get_container_element_type(0);
		append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_ARRAY);
		append(p_target);
		append(p_source);
		append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
		append(element_type.builtin_type);
		append(element_type.native_type);
	} else if (p_target.type.kind == FSDataType::BUILTIN && p_target.type.builtin_type == Variant::DICTIONARY && p_target.type.has_container_element_types()) {
		const FSDataType &key_type = p_target.type.get_container_element_type_or_variant(0);
		const FSDataType &value_type = p_target.type.get_container_element_type_or_variant(1);
		append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY);
		append(p_target);
		append(p_source);
		append(get_container_type_pos(key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
		append(get_container_type_pos(value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
		append(key_type.builtin_type);
		append(key_type.native_type);
		append(value_type.builtin_type);
		append(value_type.native_type);
	} else if (p_target.type.kind == FSDataType::BUILTIN && p_source.type.kind == FSDataType::BUILTIN && p_target.type.builtin_type != p_source.type.builtin_type) {
		// Need conversion.
		append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_BUILTIN);
		append(p_target);
		append(p_source);
		append(p_target.type.builtin_type | (p_target.type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0));
	} else {
		append_opcode(FSFunction::OPCODE_ASSIGN);
		append(p_target);
		append(p_source);
	}
}

void FSByteCodeGenerator::write_assign_typed_parameter(const Address &p_target, const Address &p_source, int p_member_index) {
	append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_PARAMETER);
	append(p_target);
	append(p_source);
	append(p_member_index);
}

void FSByteCodeGenerator::write_assign_typed_array_convert(const Address &p_target, const Address &p_source) {
	const FSDataType &element_type = p_target.type.get_container_element_type(0);
	append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_ARRAY_CONVERT);
	append(p_target);
	append(p_source);
	append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(element_type.builtin_type);
	append(element_type.native_type);
}

void FSByteCodeGenerator::write_assign_typed_dictionary_convert(const Address &p_target, const Address &p_source) {
	const FSDataType &key_type = p_target.type.get_container_element_type_or_variant(0);
	const FSDataType &value_type = p_target.type.get_container_element_type_or_variant(1);
	append_opcode(FSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT);
	append(p_target);
	append(p_source);
	append(get_container_type_pos(key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(get_container_type_pos(value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(key_type.builtin_type);
	append(key_type.native_type);
	append(value_type.builtin_type);
	append(value_type.native_type);
}

void FSByteCodeGenerator::write_assign_null(const Address &p_target) {
	append_opcode(FSFunction::OPCODE_ASSIGN_NULL);
	append(p_target);
}

void FSByteCodeGenerator::write_assign_true(const Address &p_target) {
	append_opcode(FSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
}

void FSByteCodeGenerator::write_assign_false(const Address &p_target) {
	append_opcode(FSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
}

void FSByteCodeGenerator::write_assign_default_parameter(const Address &p_dst, const Address &p_src, bool p_use_conversion) {
	if (p_use_conversion) {
		write_assign_with_conversion(p_dst, p_src);
	} else {
		write_assign(p_dst, p_src);
	}
	function->default_arguments.push_back(opcodes.size());
}

void FSByteCodeGenerator::write_store_global(const Address &p_dst, int p_global_index, const StringName &p_global_name) {
	append_opcode(FSFunction::OPCODE_STORE_GLOBAL);
	append(p_dst);
	append(p_global_index);
#ifdef TOOLS_ENABLED
	export_fixups.global_stores.push_back(FSFunction::ExportFixups::GlobalStore{ static_cast<int>(opcodes.size()) - 1, p_global_name });
#endif
}

void FSByteCodeGenerator::write_store_named_global(const Address &p_dst, const StringName &p_global) {
	append_opcode(FSFunction::OPCODE_STORE_NAMED_GLOBAL);
	append(p_dst);
	append(p_global);
#ifdef TOOLS_ENABLED
	if (!named_globals.has(p_global)) {
		named_globals.push_back(p_global);
	}
#endif
}

void FSByteCodeGenerator::write_cast(const Address &p_target, const Address &p_source, const FSDataType &p_type) {
	int index = 0;

	switch (p_type.kind) {
		case FSDataType::BUILTIN: {
			append_opcode(FSFunction::OPCODE_CAST_TO_BUILTIN);
			index = p_type.builtin_type | (p_type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0);
		} break;
		case FSDataType::NATIVE: {
			append_opcode(FSFunction::OPCODE_CAST_TO_NATIVE);
			index = get_native_type_pos(p_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
		} break;
		case FSDataType::SCRIPT:
		case FSDataType::FOUNDRY_SCRIPT: {
			Variant script = p_type.script_type;
			// See `write_type_test`: a `Self` target is cast against the frame's exact receiver.
			const bool needs_descriptor = p_type.references_self_type() || (p_type.is_type_handle && !p_type.type_arguments.is_empty());
			int idx = needs_descriptor ? get_container_type_pos(p_type) : get_constant_pos(script);
			idx |= (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
			append_opcode(FSFunction::OPCODE_CAST_TO_SCRIPT);
			index = idx;
		} break;
		default: {
			return;
		}
	}

	append(p_source);
	append(p_target);
	append(index);
	if (p_type.kind == FSDataType::NATIVE || p_type.kind == FSDataType::SCRIPT || p_type.kind == FSDataType::FOUNDRY_SCRIPT) {
		append(p_type.is_type_handle);
	}
}

FSByteCodeGenerator::CallTarget FSByteCodeGenerator::get_call_target(const FSCodeGenerator::Address &p_target, Variant::Type p_type) {
	if (p_target.mode == Address::NIL) {
		FSDataType type;
		if (p_type != Variant::NIL) {
			type.kind = FSDataType::BUILTIN;
			type.builtin_type = p_type;
		}
		uint32_t addr = add_temporary(type);
		return CallTarget(Address(Address::TEMPORARY, addr, type), true, this);
	} else {
		return CallTarget(p_target, false, this);
	}
}

#ifdef TOOLS_ENABLED
void FSByteCodeGenerator::record_reflection_call(
		const StringName &p_method, const StringName &p_class,
		bool p_receiver_is_self) {
	const uint8_t kind = FSFunction::get_reflection_kind(p_method, p_class);
	if (kind == FSFunction::REFLECTION_NONE) {
		return;
	}
	if (p_receiver_is_self && p_class != SNAME("FSReflection")) {
		function->self_reflection_kinds |= kind;
	} else {
		function->unresolved_reflection_kinds |= kind;
	}
}
#endif

void FSByteCodeGenerator::write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_function_name, StringName(), p_base.mode == Address::SELF);
#endif
	append_opcode_and_argcount(p_target.mode == Address::NIL ? FSFunction::OPCODE_CALL : FSFunction::OPCODE_CALL_RETURN, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(p_base);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(p_function_name, StringName(), true);
#endif
	append_opcode_and_argcount(FSFunction::OPCODE_CALL_SELF_BASE, 1 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_function_name, StringName(), p_base.mode == Address::SELF);
#endif
	append_opcode_and_argcount(FSFunction::OPCODE_CALL_ASYNC, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(p_base);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_enum_call(
		const Address &p_target, const Address &p_base, const Vector<Address> &p_arguments,
		const StringName &p_owner_script_path, const StringName &p_owner_class, const StringName &p_enum_type,
		const StringName &p_function_name, bool p_static, bool p_async) {
	const FSFunction::Opcode opcode = p_async ? FSFunction::OPCODE_CALL_ENUM_ASYNC
											  : (p_target.mode == Address::NIL ? FSFunction::OPCODE_CALL_ENUM
																			   : FSFunction::OPCODE_CALL_ENUM_RETURN);
	append_opcode_and_argcount(opcode, 2 + p_arguments.size());
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_base);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_owner_script_path);
	append(p_owner_class);
	append(p_enum_type);
	append(p_function_name);
	append(p_static ? 1 : 0);
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_foundry_script_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY, 1 + p_arguments.size());
	FSUtilityFunctions::FunctionPtr gds_function = FSUtilityFunctions::get_function(p_function);
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(gds_function);
	ct.cleanup();
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.gds_utilities, get_gds_utility_pos(gds_function), StringName(p_function));
#endif
#ifdef DEBUG_ENABLED
	add_debug_name(gds_utilities_names, get_gds_utility_pos(gds_function), p_function);
#endif
}

void FSByteCodeGenerator::write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	bool is_validated = true;
	if (Variant::is_utility_function_vararg(p_function)) {
		is_validated = false; // Vararg needs runtime checks, can't use validated call.
	} else if (p_arguments.size() == Variant::get_utility_function_argument_count(p_function)) {
		bool all_types_exact = true;
		for (int i = 0; i < p_arguments.size(); i++) {
			if (!IS_BUILTIN_TYPE(p_arguments[i], Variant::get_utility_function_argument_type(p_function, i))) {
				all_types_exact = false;
				break;
			}
		}

		is_validated = all_types_exact;
	}

	if (is_validated) {
		Variant::Type result_type = Variant::has_utility_function_return_value(p_function) ? Variant::get_utility_function_return_type(p_function) : Variant::NIL;
		CallTarget ct = get_call_target(p_target, result_type);
		Variant::Type temp_type = temporaries[ct.target.address].type;
		if (result_type != temp_type) {
			write_type_adjust(ct.target, result_type);
		}
		append_opcode_and_argcount(FSFunction::OPCODE_CALL_UTILITY_VALIDATED, 1 + p_arguments.size());
		for (int i = 0; i < p_arguments.size(); i++) {
			append(p_arguments[i]);
		}
		append(ct.target);
		append(p_arguments.size());
		append(Variant::get_validated_utility_function(p_function));
		ct.cleanup();
#ifdef TOOLS_ENABLED
		record_export_fixup(export_fixups.utilities, get_utility_pos(Variant::get_validated_utility_function(p_function)), StringName(p_function));
#endif
#ifdef DEBUG_ENABLED
		add_debug_name(utilities_names, get_utility_pos(Variant::get_validated_utility_function(p_function)), p_function);
#endif
	} else {
		append_opcode_and_argcount(FSFunction::OPCODE_CALL_UTILITY, 1 + p_arguments.size());
		for (int i = 0; i < p_arguments.size(); i++) {
			append(p_arguments[i]);
		}
		CallTarget ct = get_call_target(p_target);
		append(ct.target);
		append(p_arguments.size());
		append(p_function);
		ct.cleanup();
	}
}

void FSByteCodeGenerator::write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, bool p_is_static, const Vector<Address> &p_arguments) {
	bool is_validated = false;

	// Check if all types are correct.
	if (!Variant::has_builtin_method(p_type, p_method)) {
		// Not a method of this builtin at all — it is a `static` witness from a retroactive conformance
		// on the type. There is no validated function pointer to encode (asking for one yields null, and
		// the VM would call straight through it), so this has to go out as a regular call, which carries
		// the type and method name and resolves the witness at dispatch time.
		is_validated = false;
	} else if (Variant::is_builtin_method_vararg(p_type, p_method)) {
		is_validated = false; // Vararg needs runtime checks, can't use validated call.
	} else if (!p_is_static && p_base.type.is_nullable) {
		// A nullable receiver may hold null, which the validated call would read as the underlying
		// type. The regular call reports the missing method on a null base instead.
		is_validated = false;
	} else if (p_arguments.size() == Variant::get_builtin_method_argument_count(p_type, p_method)) {
		bool all_types_exact = true;
		for (int i = 0; i < p_arguments.size(); i++) {
			if (!IS_BUILTIN_TYPE(p_arguments[i], Variant::get_builtin_method_argument_type(p_type, p_method, i))) {
				all_types_exact = false;
				break;
			}
		}

		is_validated = all_types_exact;
	}

	if (!is_validated) {
		// Perform regular call.
		if (p_is_static) {
			append_opcode_and_argcount(FSFunction::OPCODE_CALL_BUILTIN_STATIC, p_arguments.size() + 1);
			for (int i = 0; i < p_arguments.size(); i++) {
				append(p_arguments[i]);
			}
			CallTarget ct = get_call_target(p_target);
			append(ct.target);
			append(p_type);
			append(p_method);
			append(p_arguments.size());
			ct.cleanup();
		} else {
			write_call(p_target, p_base, p_method, p_arguments);
		}
		return;
	}

	Variant::Type result_type = Variant::get_builtin_method_return_type(p_type, p_method);
	CallTarget ct = get_call_target(p_target, result_type);
	Variant::Type temp_type = temporaries[ct.target.address].type;
	if (result_type != temp_type) {
		write_type_adjust(ct.target, result_type);
	}

	append_opcode_and_argcount(FSFunction::OPCODE_CALL_BUILTIN_TYPE_VALIDATED, 2 + p_arguments.size());

	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(p_base);
	append(ct.target);
	append(p_arguments.size());
	Variant::ValidatedBuiltInMethod validated_method = Variant::get_validated_builtin_method(p_type, p_method);
	const int method_index = get_builtin_method_pos(validated_method);
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.builtin_methods, method_index, FSFunction::ExportFixups::TypedNameKey{ p_type, p_method });
#endif
	append(method_index);
	add_builtin_method_name(method_index, p_method);
	ct.cleanup();

#ifdef DEBUG_ENABLED
	add_debug_name(builtin_methods_names, method_index, p_method);
#endif
}

void FSByteCodeGenerator::write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	write_call_builtin_type(p_target, p_base, p_type, p_method, false, p_arguments);
}

void FSByteCodeGenerator::write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	write_call_builtin_type(p_target, Address(), p_type, p_method, true, p_arguments);
}

void FSByteCodeGenerator::write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) {
	MethodBind *method = ClassDB::get_method(p_class, p_method);

#ifdef TOOLS_ENABLED
	record_reflection_call(p_method, p_class, false);
#endif

	// Perform regular call.
	append_opcode_and_argcount(FSFunction::OPCODE_CALL_NATIVE_STATIC, p_arguments.size() + 1);
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(method);
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.method_binds, get_method_bind_pos(method), FSFunction::ExportFixups::MethodBindKey{ p_class, p_method });
#endif
	append(p_arguments.size());
	ct.cleanup();
	return;
}

void FSByteCodeGenerator::write_call_native_static_validated(const FSCodeGenerator::Address &p_target, MethodBind *p_method, const Vector<FSCodeGenerator::Address> &p_arguments) {
	Variant::Type return_type = Variant::NIL;
	bool has_return = p_method->has_return();

#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_method->get_name(), p_method->get_instance_class(), false);
#endif

	if (has_return) {
		PropertyInfo return_info = p_method->get_return_info();
		return_type = return_info.type;
	}

	CallTarget ct = get_call_target(p_target, return_type);

	if (has_return) {
		Variant::Type temp_type = temporaries[ct.target.address].type;
		if (temp_type != return_type) {
			write_type_adjust(ct.target, return_type);
		}
	}

	FSFunction::Opcode code = p_method->has_return() ? FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN : FSFunction::OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN;
	append_opcode_and_argcount(code, 1 + p_arguments.size());

	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(ct.target);
	append(p_arguments.size());
	append(p_method);
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.method_binds, get_method_bind_pos(p_method), FSFunction::ExportFixups::MethodBindKey{ p_method->get_instance_class(), p_method->get_name() });
#endif
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_method_bind(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_method->get_name(), p_method->get_instance_class(),
			p_base.mode == Address::SELF);
#endif
	append_opcode_and_argcount(p_target.mode == Address::NIL ? FSFunction::OPCODE_CALL_METHOD_BIND : FSFunction::OPCODE_CALL_METHOD_BIND_RET, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(p_base);
	append(ct.target);
	append(p_arguments.size());
	append(p_method);
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.method_binds, get_method_bind_pos(p_method), FSFunction::ExportFixups::MethodBindKey{ p_method->get_instance_class(), p_method->get_name() });
#endif
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_method_bind_validated(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) {
	Variant::Type return_type = Variant::NIL;
	bool has_return = p_method->has_return();

#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_method->get_name(), p_method->get_instance_class(),
			p_base.mode == Address::SELF);
#endif

	if (has_return) {
		PropertyInfo return_info = p_method->get_return_info();
		return_type = return_info.type;
	}

	CallTarget ct = get_call_target(p_target, return_type);

	if (has_return) {
		Variant::Type temp_type = temporaries[ct.target.address].type;
		if (temp_type != return_type) {
			write_type_adjust(ct.target, return_type);
		}
	}

	FSFunction::Opcode code = p_method->has_return() ? FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN : FSFunction::OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN;
	append_opcode_and_argcount(code, 2 + p_arguments.size());

	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(p_base);
	append(ct.target);
	append(p_arguments.size());
	append(p_method);
#ifdef TOOLS_ENABLED
	record_export_fixup(export_fixups.method_binds, get_method_bind_pos(p_method), FSFunction::ExportFixups::MethodBindKey{ p_method->get_instance_class(), p_method->get_name() });
#endif
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(p_function_name, StringName(), true);
#endif
	append_opcode_and_argcount(p_target.mode == Address::NIL ? FSFunction::OPCODE_CALL : FSFunction::OPCODE_CALL_RETURN, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(FSFunction::ADDR_TYPE_STACK << FSFunction::ADDR_BITS);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_self_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(p_function_name, StringName(), true);
#endif
	append_opcode_and_argcount(FSFunction::OPCODE_CALL_ASYNC, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(FSFunction::ADDR_SELF);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
#ifdef TOOLS_ENABLED
	record_reflection_call(
			p_function_name, StringName(), p_base.mode == Address::SELF);
#endif
	append_opcode_and_argcount(p_target.mode == Address::NIL ? FSFunction::OPCODE_CALL : FSFunction::OPCODE_CALL_RETURN, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	append(p_base);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_function_name);
	ct.cleanup();
}

void FSByteCodeGenerator::write_lambda(const Address &p_target, FSFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) {
	append_opcode_and_argcount(p_use_self ? FSFunction::OPCODE_CREATE_SELF_LAMBDA : FSFunction::OPCODE_CREATE_LAMBDA, 1 + p_captures.size());
	for (int i = 0; i < p_captures.size(); i++) {
		append(p_captures[i]);
	}

	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_captures.size());
	append(p_function);
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) {
	// Try to find an appropriate constructor.
	bool all_have_type = true;
	Vector<Variant::Type> arg_types;
	for (int i = 0; i < p_arguments.size(); i++) {
		if (!HAS_BUILTIN_TYPE(p_arguments[i])) {
			all_have_type = false;
			break;
		}
		arg_types.push_back(p_arguments[i].type.builtin_type);
	}
	if (all_have_type) {
		int valid_constructor = -1;
		for (int i = 0; i < Variant::get_constructor_count(p_type); i++) {
			if (Variant::get_constructor_argument_count(p_type, i) != p_arguments.size()) {
				continue;
			}
			int types_correct = true;
			for (int j = 0; j < arg_types.size(); j++) {
				if (arg_types[j] != Variant::get_constructor_argument_type(p_type, i, j)) {
					types_correct = false;
					break;
				}
			}
			if (types_correct) {
				valid_constructor = i;
				break;
			}
		}
		if (valid_constructor >= 0) {
			append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_VALIDATED, 1 + p_arguments.size());
			for (int i = 0; i < p_arguments.size(); i++) {
				append(p_arguments[i]);
			}
			CallTarget ct = get_call_target(p_target);
			append(ct.target);
			append(p_arguments.size());
			append(Variant::get_validated_constructor(p_type, valid_constructor));
			ct.cleanup();
#ifdef TOOLS_ENABLED
			record_export_fixup(export_fixups.constructors, get_constructor_pos(Variant::get_validated_constructor(p_type, valid_constructor)), FSFunction::ExportFixups::ConstructorKey{ p_type, valid_constructor });
#endif
#ifdef DEBUG_ENABLED
			add_debug_name(constructors_names, get_constructor_pos(Variant::get_validated_constructor(p_type, valid_constructor)), Variant::get_type_name(p_type));
#endif
			return;
		}
	}

	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT, 1 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_type);
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_ARRAY, 1 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct_tuple(const Address &p_target, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_TUPLE, 1 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct_typed_array(const Address &p_target, const FSDataType &p_element_type, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_TYPED_ARRAY, 2 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(get_container_type_pos(p_element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(p_arguments.size());
	append(p_element_type.builtin_type);
	append(p_element_type.native_type);
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct_specialized(const Address &p_target, const Address &p_base, const Address &p_expected_base, const Vector<FSDataType> &p_type_arguments, const Vector<Address> &p_arguments) {
	// Instruction args: [ctor args..., type-argument descriptors..., base script, expected base script, target].
	// Inline operands: argument count, type-argument count.
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_SPECIALIZED, 3 + p_arguments.size() + p_type_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	for (int i = 0; i < p_type_arguments.size(); i++) {
		append(get_constant_pos(make_container_type_descriptor(p_type_arguments[i])) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	}
	append(p_base);
	append(p_expected_base);
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size());
	append(p_type_arguments.size());
	ct.cleanup();
}

void FSByteCodeGenerator::write_load_static_self_class(const Address &p_target) {
	append_opcode(FSFunction::OPCODE_LOAD_STATIC_SELF_CLASS);
	append(p_target);
}

void FSByteCodeGenerator::write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_DICTIONARY, 1 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(p_arguments.size() / 2); // This is number of key-value pairs, so only half of actual arguments.
	ct.cleanup();
}

void FSByteCodeGenerator::write_construct_typed_dictionary(const Address &p_target, const FSDataType &p_key_type, const FSDataType &p_value_type, const Vector<Address> &p_arguments) {
	append_opcode_and_argcount(FSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY, 3 + p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		append(p_arguments[i]);
	}
	CallTarget ct = get_call_target(p_target);
	append(ct.target);
	append(get_container_type_pos(p_key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(get_container_type_pos(p_value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
	append(p_arguments.size() / 2); // This is number of key-value pairs, so only half of actual arguments.
	append(p_key_type.builtin_type);
	append(p_key_type.native_type);
	append(p_value_type.builtin_type);
	append(p_value_type.native_type);
	ct.cleanup();
}

void FSByteCodeGenerator::write_await(const Address &p_target, const Address &p_operand) {
	append_opcode(FSFunction::OPCODE_AWAIT);
	append(p_operand);
	append_opcode(FSFunction::OPCODE_AWAIT_RESUME);
	append(p_target);
}

void FSByteCodeGenerator::write_if(const Address &p_condition) {
	append_opcode(FSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	if_jmp_addrs.push_back(opcodes.size());
	append(0); // Jump destination, will be patched.
}

void FSByteCodeGenerator::write_else() {
	append_opcode(FSFunction::OPCODE_JUMP); // Jump from true if block;
	int else_jmp_addr = opcodes.size();
	append(0); // Jump destination, will be patched.

	patch_jump(if_jmp_addrs.back()->get());
	if_jmp_addrs.pop_back();
	if_jmp_addrs.push_back(else_jmp_addr);
}

void FSByteCodeGenerator::write_endif() {
	patch_jump(if_jmp_addrs.back()->get());
	if_jmp_addrs.pop_back();
}

void FSByteCodeGenerator::write_jump_if_shared(const Address &p_value) {
	append_opcode(FSFunction::OPCODE_JUMP_IF_SHARED);
	append(p_value);
	if_jmp_addrs.push_back(opcodes.size());
	append(0); // Jump destination, will be patched.
}

void FSByteCodeGenerator::write_end_jump_if_shared() {
	patch_jump(if_jmp_addrs.back()->get());
	if_jmp_addrs.pop_back();
}

void FSByteCodeGenerator::start_for(const FSDataType &p_iterator_type, const FSDataType &p_list_type, bool p_is_range) {
	Address counter(Address::LOCAL_VARIABLE, add_local("@counter_pos", p_iterator_type), p_iterator_type);

	// Store state.
	for_counter_variables.push_back(counter);

	if (p_is_range) {
		FSDataType int_type;
		int_type.kind = FSDataType::BUILTIN;
		int_type.builtin_type = Variant::INT;

		Address range_from(Address::LOCAL_VARIABLE, add_local("@range_from", int_type), int_type);
		Address range_to(Address::LOCAL_VARIABLE, add_local("@range_to", int_type), int_type);
		Address range_step(Address::LOCAL_VARIABLE, add_local("@range_step", int_type), int_type);

		// Store state.
		for_range_from_variables.push_back(range_from);
		for_range_to_variables.push_back(range_to);
		for_range_step_variables.push_back(range_step);
	} else {
		Address container(Address::LOCAL_VARIABLE, add_local("@container_pos", p_list_type), p_list_type);

		// Store state.
		for_container_variables.push_back(container);
	}
}

void FSByteCodeGenerator::write_for_list_assignment(const Address &p_list) {
	const Address &container = for_container_variables.back()->get();

	// Assign container.
	append_opcode(FSFunction::OPCODE_ASSIGN);
	append(container);
	append(p_list);
}

void FSByteCodeGenerator::write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) {
	const Address &range_from = for_range_from_variables.back()->get();
	const Address &range_to = for_range_to_variables.back()->get();
	const Address &range_step = for_range_step_variables.back()->get();

	// Assign range args.
	if (range_from.type == p_from.type) {
		write_assign(range_from, p_from);
	} else {
		write_assign_with_conversion(range_from, p_from);
	}
	if (range_to.type == p_to.type) {
		write_assign(range_to, p_to);
	} else {
		write_assign_with_conversion(range_to, p_to);
	}
	if (range_step.type == p_step.type) {
		write_assign(range_step, p_step);
	} else {
		write_assign_with_conversion(range_step, p_step);
	}
}

void FSByteCodeGenerator::write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) {
	const Address &counter = for_counter_variables.back()->get();
	const Address &container = p_is_range ? Address() : for_container_variables.back()->get();
	const Address &range_from = p_is_range ? for_range_from_variables.back()->get() : Address();
	const Address &range_to = p_is_range ? for_range_to_variables.back()->get() : Address();
	const Address &range_step = p_is_range ? for_range_step_variables.back()->get() : Address();

	current_breaks_to_patch.push_back(List<int>());

	FSFunction::Opcode begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN;
	FSFunction::Opcode iterate_opcode = FSFunction::OPCODE_ITERATE;

	if (p_is_range) {
		begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_RANGE;
		iterate_opcode = FSFunction::OPCODE_ITERATE_RANGE;
	} else if (container.type.has_type()) {
		if (container.type.kind == FSDataType::BUILTIN) {
			switch (container.type.builtin_type) {
				case Variant::INT:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_INT;
					iterate_opcode = FSFunction::OPCODE_ITERATE_INT;
					break;
				case Variant::FLOAT:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_FLOAT;
					iterate_opcode = FSFunction::OPCODE_ITERATE_FLOAT;
					break;
				case Variant::VECTOR2:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_VECTOR2;
					iterate_opcode = FSFunction::OPCODE_ITERATE_VECTOR2;
					break;
				case Variant::VECTOR2I:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_VECTOR2I;
					iterate_opcode = FSFunction::OPCODE_ITERATE_VECTOR2I;
					break;
				case Variant::VECTOR3:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_VECTOR3;
					iterate_opcode = FSFunction::OPCODE_ITERATE_VECTOR3;
					break;
				case Variant::VECTOR3I:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_VECTOR3I;
					iterate_opcode = FSFunction::OPCODE_ITERATE_VECTOR3I;
					break;
				case Variant::STRING:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_STRING;
					iterate_opcode = FSFunction::OPCODE_ITERATE_STRING;
					break;
				case Variant::DICTIONARY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_DICTIONARY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_DICTIONARY;
					break;
				case Variant::ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_ARRAY;
					break;
				case Variant::PACKED_BYTE_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_BYTE_ARRAY;
					break;
				case Variant::PACKED_INT32_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_INT32_ARRAY;
					break;
				case Variant::PACKED_INT64_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_INT64_ARRAY;
					break;
				case Variant::PACKED_FLOAT32_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_FLOAT32_ARRAY;
					break;
				case Variant::PACKED_FLOAT64_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_FLOAT64_ARRAY;
					break;
				case Variant::PACKED_STRING_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_STRING_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_STRING_ARRAY;
					break;
				case Variant::PACKED_VECTOR2_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_VECTOR2_ARRAY;
					break;
				case Variant::PACKED_VECTOR3_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_VECTOR3_ARRAY;
					break;
				case Variant::PACKED_COLOR_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_COLOR_ARRAY;
					break;
				case Variant::PACKED_VECTOR4_ARRAY:
					begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY;
					iterate_opcode = FSFunction::OPCODE_ITERATE_PACKED_VECTOR4_ARRAY;
					break;
				default:
					break;
			}
		} else {
			begin_opcode = FSFunction::OPCODE_ITERATE_BEGIN_OBJECT;
			iterate_opcode = FSFunction::OPCODE_ITERATE_OBJECT;
		}
	}

	Address temp;
	if (p_use_conversion) {
		temp = Address(Address::LOCAL_VARIABLE, add_local("@iterator_temp", FSDataType()));
	}

	// Begin loop.
	append_opcode(begin_opcode);
	append(counter);
	if (p_is_range) {
		append(range_from);
		append(range_to);
		append(range_step);
	} else {
		append(container);
	}
	append(p_use_conversion ? temp : p_variable);
	for_jmp_addrs.push_back(opcodes.size());
	append(0); // End of loop address, will be patched.
	append_opcode(FSFunction::OPCODE_JUMP);
	append(opcodes.size() + (p_is_range ? 7 : 6)); // Skip over 'continue' code.

	// Next iteration.
	int continue_addr = opcodes.size();
	continue_addrs.push_back(continue_addr);
	append_opcode(iterate_opcode);
	append(counter);
	if (p_is_range) {
		append(range_to);
		append(range_step);
	} else {
		append(container);
	}
	append(p_use_conversion ? temp : p_variable);
	for_jmp_addrs.push_back(opcodes.size());
	append(0); // Jump destination, will be patched.

	if (p_use_conversion) {
		write_assign_with_conversion(p_variable, temp);
		if (p_variable.type.can_contain_object()) {
			clear_address(temp); // Can contain `RefCounted`, so clear it.
		}
	}
}

void FSByteCodeGenerator::write_endfor(bool p_is_range) {
	// Jump back to loop check.
	append_opcode(FSFunction::OPCODE_JUMP);
	append(continue_addrs.back()->get());
	continue_addrs.pop_back();

	// Patch end jumps (two of them).
	for (int i = 0; i < 2; i++) {
		patch_jump(for_jmp_addrs.back()->get());
		for_jmp_addrs.pop_back();
	}

	// Patch break statements.
	for (const int &E : current_breaks_to_patch.back()->get()) {
		patch_jump(E);
	}
	current_breaks_to_patch.pop_back();

	// Pop state.
	for_counter_variables.pop_back();
	if (p_is_range) {
		for_range_from_variables.pop_back();
		for_range_to_variables.pop_back();
		for_range_step_variables.pop_back();
	} else {
		for_container_variables.pop_back();
	}
}

void FSByteCodeGenerator::start_while_condition() {
	current_breaks_to_patch.push_back(List<int>());
	continue_addrs.push_back(opcodes.size());
}

void FSByteCodeGenerator::write_while(const Address &p_condition) {
	// Condition check.
	append_opcode(FSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	while_jmp_addrs.push_back(opcodes.size());
	append(0); // End of loop address, will be patched.
}

void FSByteCodeGenerator::write_endwhile() {
	// Jump back to loop check.
	append_opcode(FSFunction::OPCODE_JUMP);
	append(continue_addrs.back()->get());
	continue_addrs.pop_back();

	// Patch end jump.
	patch_jump(while_jmp_addrs.back()->get());
	while_jmp_addrs.pop_back();

	// Patch break statements.
	for (const int &E : current_breaks_to_patch.back()->get()) {
		patch_jump(E);
	}
	current_breaks_to_patch.pop_back();
}

void FSByteCodeGenerator::write_break() {
	append_opcode(FSFunction::OPCODE_JUMP);
	current_breaks_to_patch.back()->get().push_back(opcodes.size());
	append(0);
}

void FSByteCodeGenerator::write_continue() {
	append_opcode(FSFunction::OPCODE_JUMP);
	append(continue_addrs.back()->get());
}

void FSByteCodeGenerator::write_breakpoint() {
	append_opcode(FSFunction::OPCODE_BREAKPOINT);
}

void FSByteCodeGenerator::write_newline(int p_line) {
	if (FSLanguage::get_singleton()->should_track_call_stack()) {
		// Add newline for debugger and stack tracking if enabled in the project settings.
		append_opcode(FSFunction::OPCODE_LINE);
		append(p_line);
		current_line = p_line;
	}
}

void FSByteCodeGenerator::write_return(const Address &p_return_value) {
	if (!function->return_type.has_type() || p_return_value.type.has_type()) {
		// Either the function is untyped or the return value is also typed.

		// If this is a typed function, then we need to check for potential conversions.
		if (function->return_type.has_type()) {
			if (!function->return_type.is_type_handle && function->return_type.kind == FSDataType::NATIVE &&
					p_return_value.type.is_type_handle && !p_return_value.type.type_arguments.is_empty() &&
					ClassDB::is_parent_class(SNAME("FoundryScript"), function->return_type.native_type)) {
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_NATIVE);
				append(p_return_value);
				int class_idx = FSLanguage::get_singleton()->get_global_map()[function->return_type.native_type];
				Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
				class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
				append(class_idx);
				append(false);
			} else if (function->return_type.is_type_handle && function->return_type.kind == FSDataType::NATIVE) {
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_NATIVE);
				append(p_return_value);
				int class_idx = FSLanguage::get_singleton()->get_global_map()[function->return_type.native_type];
				Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
				class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
				append(class_idx);
				append(true);
			} else if (function->return_type.is_type_handle && (function->return_type.kind == FSDataType::SCRIPT || function->return_type.kind == FSDataType::FOUNDRY_SCRIPT)) {
				Variant script = function->return_type.script_type;
				int script_idx = !function->return_type.type_arguments.is_empty() ? get_container_type_pos(function->return_type) : get_constant_pos(script);
				script_idx |= (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);

				append_opcode(FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
				append(p_return_value);
				append(script_idx);
				append(true);
			} else if (function->return_type.kind == FSDataType::BUILTIN && function->return_type.builtin_type == Variant::ARRAY && function->return_type.has_container_element_type(0)) {
				// Typed array.
				const FSDataType &element_type = function->return_type.get_container_element_type(0);
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_ARRAY);
				append(p_return_value);
				append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(element_type.builtin_type);
				append(element_type.native_type);
			} else if (function->return_type.kind == FSDataType::BUILTIN && function->return_type.builtin_type == Variant::DICTIONARY &&
					function->return_type.has_container_element_types()) {
				// Typed dictionary.
				const FSDataType &key_type = function->return_type.get_container_element_type_or_variant(0);
				const FSDataType &value_type = function->return_type.get_container_element_type_or_variant(1);
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_DICTIONARY);
				append(p_return_value);
				append(get_container_type_pos(key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(get_container_type_pos(value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
				append(key_type.builtin_type);
				append(key_type.native_type);
				append(value_type.builtin_type);
				append(value_type.native_type);
			} else if (function->return_type.kind == FSDataType::BUILTIN && p_return_value.type.kind == FSDataType::BUILTIN && function->return_type.builtin_type != p_return_value.type.builtin_type) {
				// Add conversion.
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_BUILTIN);
				append(p_return_value);
				append(function->return_type.builtin_type | (function->return_type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0));
			} else {
				// Just assign.
				append_opcode(FSFunction::OPCODE_RETURN);
				append(p_return_value);
			}
		} else {
			append_opcode(FSFunction::OPCODE_RETURN);
			append(p_return_value);
		}
	} else {
		switch (function->return_type.kind) {
			case FSDataType::BUILTIN: {
				if (function->return_type.builtin_type == Variant::ARRAY && function->return_type.has_container_element_type(0)) {
					const FSDataType &element_type = function->return_type.get_container_element_type(0);
					append_opcode(FSFunction::OPCODE_RETURN_TYPED_ARRAY);
					append(p_return_value);
					append(get_container_type_pos(element_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
					append(element_type.builtin_type);
					append(element_type.native_type);
				} else if (function->return_type.builtin_type == Variant::DICTIONARY && function->return_type.has_container_element_types()) {
					const FSDataType &key_type = function->return_type.get_container_element_type_or_variant(0);
					const FSDataType &value_type = function->return_type.get_container_element_type_or_variant(1);
					append_opcode(FSFunction::OPCODE_RETURN_TYPED_DICTIONARY);
					append(p_return_value);
					append(get_container_type_pos(key_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
					append(get_container_type_pos(value_type) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS));
					append(key_type.builtin_type);
					append(key_type.native_type);
					append(value_type.builtin_type);
					append(value_type.native_type);
				} else {
					append_opcode(FSFunction::OPCODE_RETURN_TYPED_BUILTIN);
					append(p_return_value);
					append(function->return_type.builtin_type | (function->return_type.is_nullable ? FSFunction::NULLABLE_TYPE_OPERAND_FLAG : 0));
				}
			} break;
			case FSDataType::NATIVE: {
				append_opcode(FSFunction::OPCODE_RETURN_TYPED_NATIVE);
				append(p_return_value);
				int class_idx = FSLanguage::get_singleton()->get_global_map()[function->return_type.native_type];
				Variant nc = FSLanguage::get_singleton()->get_global_array()[class_idx];
				class_idx = get_constant_pos(nc) | (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);
				append(class_idx);
				append(function->return_type.is_type_handle);
			} break;
			case FSDataType::FOUNDRY_SCRIPT:
			case FSDataType::SCRIPT: {
				Variant script = function->return_type.script_type;
				int script_idx = function->return_type.is_type_handle && !function->return_type.type_arguments.is_empty() ? get_container_type_pos(function->return_type) : get_constant_pos(script);
				script_idx |= (FSFunction::ADDR_TYPE_CONSTANT << FSFunction::ADDR_BITS);

				append_opcode(FSFunction::OPCODE_RETURN_TYPED_SCRIPT);
				append(p_return_value);
				append(script_idx);
				append(function->return_type.is_type_handle);
			} break;
			default: {
				ERR_PRINT("Compiler bug: unresolved return.");

				// Shouldn't get here, but fail-safe to a regular return;
				append_opcode(FSFunction::OPCODE_RETURN);
				append(p_return_value);
			} break;
		}
	}
}

void FSByteCodeGenerator::write_assert(const Address &p_test, const Address &p_message) {
	append_opcode(FSFunction::OPCODE_ASSERT);
	append(p_test);
	append(p_message);
}

void FSByteCodeGenerator::start_block() {
	push_stack_identifiers();
}

void FSByteCodeGenerator::end_block() {
	pop_stack_identifiers();
}

void FSByteCodeGenerator::clear_temporaries() {
	for (int slot_idx : temporaries_pending_clear) {
		// The temporary may have been reused as something else since it was added to the list.
		// In that case, there's **no** need to clear it.
		if (temporaries[slot_idx].can_contain_object) {
			clear_address(Address(Address::TEMPORARY, slot_idx)); // Can contain `RefCounted`, so clear it.
		}
	}
	temporaries_pending_clear.clear();
}

void FSByteCodeGenerator::clear_address(const Address &p_address) {
	// Do not check `is_local_dirty()` here! Always clear the address since the codegen doesn't track the compiler.
	// Also, this method is used to initialize local variables of built-in types, since they cannot be `null`.

	if (p_address.type.kind == FSDataType::BUILTIN) {
		switch (p_address.type.builtin_type) {
			case Variant::BOOL:
				write_assign_false(p_address);
				break;
			case Variant::DICTIONARY:
				if (p_address.type.has_container_element_types()) {
					write_construct_typed_dictionary(p_address, p_address.type.get_container_element_type_or_variant(0), p_address.type.get_container_element_type_or_variant(1), Vector<FSCodeGenerator::Address>());
				} else {
					write_construct(p_address, p_address.type.builtin_type, Vector<FSCodeGenerator::Address>());
				}
				break;
			case Variant::ARRAY:
				if (p_address.type.has_container_element_type(0)) {
					write_construct_typed_array(p_address, p_address.type.get_container_element_type(0), Vector<FSCodeGenerator::Address>());
				} else {
					write_construct(p_address, p_address.type.builtin_type, Vector<FSCodeGenerator::Address>());
				}
				break;
			case Variant::NIL:
			case Variant::OBJECT:
				write_assign_null(p_address);
				break;
			default:
				write_construct(p_address, p_address.type.builtin_type, Vector<FSCodeGenerator::Address>());
				break;
		}
	} else {
		write_assign_null(p_address);
	}

	if (p_address.mode == Address::LOCAL_VARIABLE) {
		dirty_locals.erase(p_address.address);
	}
}

// Returns `true` if the local has been reused and not cleaned up with `clear_address()`.
bool FSByteCodeGenerator::is_local_dirty(const Address &p_address) const {
	ERR_FAIL_COND_V(p_address.mode != Address::LOCAL_VARIABLE, false);
	return dirty_locals.has(p_address.address);
}

FSByteCodeGenerator::~FSByteCodeGenerator() {
	if (!ended && function != nullptr) {
		memdelete(function);
	}
}
