/**************************************************************************/
/*  foundry_extension_interface.cpp                                       */
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

#include "foundry_extension_interface.gen.h"

#include "core/config/engine.h"
#include "core/extension/foundry_extension.h"
#include "core/extension/foundry_extension_special_compat_hashes.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/xml_parser.h"
#include "core/object/class_db.h"
#include "core/object/script_language_extension.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/memory.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"
#include "core/version.h"

class CallableCustomExtension : public CallableCustom {
	void *userdata;
	void *token;

	ObjectID object;

	FoundryExtensionCallableCustomCall call_func;
	FoundryExtensionCallableCustomIsValid is_valid_func;
	FoundryExtensionCallableCustomFree free_func;

	FoundryExtensionCallableCustomEqual equal_func;
	FoundryExtensionCallableCustomLessThan less_than_func;

	FoundryExtensionCallableCustomToString to_string_func;

	FoundryExtensionCallableCustomGetArgumentCount get_argument_count_func;

	uint32_t _hash;

	static bool default_compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const CallableCustomExtension *a = static_cast<const CallableCustomExtension *>(p_a);
		const CallableCustomExtension *b = static_cast<const CallableCustomExtension *>(p_b);

		if (a->call_func != b->call_func || a->userdata != b->userdata) {
			return false;
		}
		return true;
	}

	static bool default_compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const CallableCustomExtension *a = static_cast<const CallableCustomExtension *>(p_a);
		const CallableCustomExtension *b = static_cast<const CallableCustomExtension *>(p_b);

		if (a->call_func != b->call_func) {
			return a->call_func < b->call_func;
		}
		return a->userdata < b->userdata;
	}

	static bool custom_compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const CallableCustomExtension *a = static_cast<const CallableCustomExtension *>(p_a);
		const CallableCustomExtension *b = static_cast<const CallableCustomExtension *>(p_b);

		if (a->equal_func != b->equal_func) {
			return false;
		}
		return a->equal_func(a->userdata, b->userdata);
	}

	static bool custom_compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const CallableCustomExtension *a = static_cast<const CallableCustomExtension *>(p_a);
		const CallableCustomExtension *b = static_cast<const CallableCustomExtension *>(p_b);

		if (a->less_than_func != b->less_than_func) {
			return default_compare_less(p_a, p_b);
		}
		return a->less_than_func(a->userdata, b->userdata);
	}

public:
	uint32_t hash() const override {
		return _hash;
	}

	String get_as_text() const override {
		if (to_string_func != nullptr) {
			String out;
			FoundryExtensionBool is_valid = false;

			to_string_func(userdata, &is_valid, (FoundryExtensionStringPtr)&out);

			if (is_valid) {
				return out;
			}
		}
		return "<CallableCustom>";
	}

	CompareEqualFunc get_compare_equal_func() const override {
		return (equal_func != nullptr) ? custom_compare_equal : default_compare_equal;
	}

	CompareLessFunc get_compare_less_func() const override {
		return (less_than_func != nullptr) ? custom_compare_less : default_compare_less;
	}

	bool is_valid() const override {
		if (is_valid_func != nullptr && !is_valid_func(userdata)) {
			return false;
		}
		return call_func != nullptr;
	}

	StringName get_method() const override {
		return StringName();
	}

	ObjectID get_object() const override {
		return object;
	}

	int get_argument_count(bool &r_is_valid) const override {
		if (get_argument_count_func != nullptr) {
			FoundryExtensionBool is_valid = false;

			FoundryExtensionInt ret = get_argument_count_func(userdata, &is_valid);

			if (is_valid) {
				r_is_valid = true;
				return ret;
			}
		}
		r_is_valid = false;
		return 0;
	}

	void *get_userdata(void *p_token) const {
		return (p_token == token) ? userdata : nullptr;
	}

	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override {
		FoundryExtensionCallError error;

		call_func(userdata, (FoundryExtensionConstVariantPtr *)p_arguments, p_argcount, (FoundryExtensionVariantPtr)&r_return_value, &error);

		r_call_error.error = (Callable::CallError::Error)error.error;
		r_call_error.argument = error.argument;
		r_call_error.expected = error.expected;
	}

#ifndef DISABLE_DEPRECATED
	CallableCustomExtension(FoundryExtensionCallableCustomInfo *p_info) {
		userdata = p_info->callable_userdata;
		token = p_info->token;

		object = p_info->object_id;

		call_func = p_info->call_func;
		is_valid_func = p_info->is_valid_func;
		free_func = p_info->free_func;

		equal_func = p_info->equal_func;
		less_than_func = p_info->less_than_func;

		to_string_func = p_info->to_string_func;

		get_argument_count_func = nullptr;

		// Pre-calculate the hash.
		if (p_info->hash_func != nullptr) {
			_hash = p_info->hash_func(userdata);
		} else {
			_hash = hash_murmur3_one_64((uint64_t)call_func);
			_hash = hash_murmur3_one_64((uint64_t)userdata, _hash);
		}
	}
#endif

	CallableCustomExtension(FoundryExtensionCallableCustomInfo2 *p_info) {
		userdata = p_info->callable_userdata;
		token = p_info->token;

		object = p_info->object_id;

		call_func = p_info->call_func;
		is_valid_func = p_info->is_valid_func;
		free_func = p_info->free_func;

		equal_func = p_info->equal_func;
		less_than_func = p_info->less_than_func;

		to_string_func = p_info->to_string_func;

		get_argument_count_func = p_info->get_argument_count_func;

		// Pre-calculate the hash.
		if (p_info->hash_func != nullptr) {
			_hash = p_info->hash_func(userdata);
		} else {
			_hash = hash_murmur3_one_64((uint64_t)call_func);
			_hash = hash_murmur3_one_64((uint64_t)userdata, _hash);
		}
	}

	~CallableCustomExtension() {
		if (free_func != nullptr) {
			free_func(userdata);
		}
	}
};

// Core interface functions.
FoundryExtensionInterfaceFunctionPtr foundry_extension_get_proc_address(const char *p_name) {
	return FoundryExtension::get_interface_function(p_name);
}

#ifndef DISABLE_DEPRECATED
static void foundry_extension_get_foundry_version(FoundryExtensionFoundryVersion *r_foundry_version) {
	r_foundry_version->major = FOUNDRY_VERSION_MAJOR;
	r_foundry_version->minor = FOUNDRY_VERSION_MINOR;
	r_foundry_version->patch = FOUNDRY_VERSION_PATCH;
	r_foundry_version->string = FOUNDRY_VERSION_FULL_NAME;
}
#endif

static void foundry_extension_get_foundry_version2(FoundryExtensionFoundryVersion2 *r_foundry_version) {
	r_foundry_version->major = FOUNDRY_VERSION_MAJOR;
	r_foundry_version->minor = FOUNDRY_VERSION_MINOR;
	r_foundry_version->patch = FOUNDRY_VERSION_PATCH;
	r_foundry_version->hex = FOUNDRY_VERSION_HEX;
	r_foundry_version->status = FOUNDRY_VERSION_STATUS;
	r_foundry_version->build = FOUNDRY_VERSION_BUILD;
	r_foundry_version->hash = FOUNDRY_VERSION_HASH;
	r_foundry_version->timestamp = FOUNDRY_VERSION_TIMESTAMP;
	r_foundry_version->string = FOUNDRY_VERSION_FULL_NAME;
}

// Memory Functions
#ifndef DISABLE_DEPRECATED
static void *foundry_extension_mem_alloc(size_t p_size) {
	return memalloc(p_size);
}

static void *foundry_extension_mem_realloc(void *p_mem, size_t p_size) {
	return memrealloc(p_mem, p_size);
}

static void foundry_extension_mem_free(void *p_mem) {
	memfree(p_mem);
}
#endif

static void *foundry_extension_mem_alloc2(size_t p_size, FoundryExtensionBool p_prepad_align) {
	return Memory::alloc_static(p_size, p_prepad_align);
}

static void *foundry_extension_mem_realloc2(void *p_mem, size_t p_size, FoundryExtensionBool p_prepad_align) {
	return Memory::realloc_static(p_mem, p_size, p_prepad_align);
}

static void foundry_extension_mem_free2(void *p_mem, FoundryExtensionBool p_prepad_align) {
	Memory::free_static(p_mem, p_prepad_align);
}

// Helper print functions.
static void foundry_extension_print_error(const char *p_description, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_editor_notify, ERR_HANDLER_ERROR);
}
static void foundry_extension_print_error_with_message(const char *p_description, const char *p_message, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_message, p_editor_notify, ERR_HANDLER_ERROR);
}
static void foundry_extension_print_warning(const char *p_description, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_editor_notify, ERR_HANDLER_WARNING);
}
static void foundry_extension_print_warning_with_message(const char *p_description, const char *p_message, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_message, p_editor_notify, ERR_HANDLER_WARNING);
}
static void foundry_extension_print_script_error(const char *p_description, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_editor_notify, ERR_HANDLER_SCRIPT);
}
static void foundry_extension_print_script_error_with_message(const char *p_description, const char *p_message, const char *p_function, const char *p_file, int32_t p_line, FoundryExtensionBool p_editor_notify) {
	_err_print_error(p_function, p_file, p_line, p_description, p_message, p_editor_notify, ERR_HANDLER_SCRIPT);
}

uint64_t foundry_extension_get_native_struct_size(FoundryExtensionConstStringNamePtr p_name) {
	const StringName name = *reinterpret_cast<const StringName *>(p_name);
	return ClassDB::get_native_struct_size(name);
}

// Variant functions

static void foundry_extension_variant_new_copy(FoundryExtensionUninitializedVariantPtr r_dest, FoundryExtensionConstVariantPtr p_src) {
	memnew_placement(reinterpret_cast<Variant *>(r_dest), Variant(*reinterpret_cast<const Variant *>(p_src)));
}
static void foundry_extension_variant_new_nil(FoundryExtensionUninitializedVariantPtr r_dest) {
	memnew_placement(reinterpret_cast<Variant *>(r_dest), Variant);
}
static void foundry_extension_variant_destroy(FoundryExtensionVariantPtr p_self) {
	reinterpret_cast<Variant *>(p_self)->~Variant();
}

// variant type

static void foundry_extension_variant_call(FoundryExtensionVariantPtr p_self, FoundryExtensionConstStringNamePtr p_method, const FoundryExtensionConstVariantPtr *p_args, FoundryExtensionInt p_argcount, FoundryExtensionUninitializedVariantPtr r_return, FoundryExtensionCallError *r_error) {
	Variant *self = (Variant *)p_self;
	const StringName method = *reinterpret_cast<const StringName *>(p_method);
	const Variant **args = (const Variant **)p_args;
	Callable::CallError error;
	memnew_placement(r_return, Variant);
	Variant *ret = reinterpret_cast<Variant *>(r_return);
	self->callp(method, args, p_argcount, *ret, error);

	if (r_error) {
		r_error->error = (FoundryExtensionCallErrorType)(error.error);
		r_error->argument = error.argument;
		r_error->expected = error.expected;
	}
}

static void foundry_extension_variant_call_static(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_method, const FoundryExtensionConstVariantPtr *p_args, FoundryExtensionInt p_argcount, FoundryExtensionUninitializedVariantPtr r_return, FoundryExtensionCallError *r_error) {
	Variant::Type type = (Variant::Type)p_type;
	const StringName method = *reinterpret_cast<const StringName *>(p_method);
	const Variant **args = (const Variant **)p_args;
	Callable::CallError error;
	memnew_placement(r_return, Variant);
	Variant *ret = reinterpret_cast<Variant *>(r_return);
	Variant::call_static(type, method, args, p_argcount, *ret, error);

	if (r_error) {
		r_error->error = (FoundryExtensionCallErrorType)error.error;
		r_error->argument = error.argument;
		r_error->expected = error.expected;
	}
}

static void foundry_extension_variant_evaluate(FoundryExtensionVariantOperator p_op, FoundryExtensionConstVariantPtr p_a, FoundryExtensionConstVariantPtr p_b, FoundryExtensionUninitializedVariantPtr r_return, FoundryExtensionBool *r_valid) {
	Variant::Operator op = (Variant::Operator)p_op;
	const Variant *a = (const Variant *)p_a;
	const Variant *b = (const Variant *)p_b;
	bool valid;
	memnew_placement(r_return, Variant);
	Variant *ret = reinterpret_cast<Variant *>(r_return);
	Variant::evaluate(op, *a, *b, *ret, valid);
	*r_valid = valid;
}

static void foundry_extension_variant_set(FoundryExtensionVariantPtr p_self, FoundryExtensionConstVariantPtr p_key, FoundryExtensionConstVariantPtr p_value, FoundryExtensionBool *r_valid) {
	Variant *self = (Variant *)p_self;
	const Variant *key = (const Variant *)p_key;
	const Variant *value = (const Variant *)p_value;

	bool valid;
	self->set(*key, *value, &valid);
	*r_valid = valid;
}

static void foundry_extension_variant_set_named(FoundryExtensionVariantPtr p_self, FoundryExtensionConstStringNamePtr p_key, FoundryExtensionConstVariantPtr p_value, FoundryExtensionBool *r_valid) {
	Variant *self = (Variant *)p_self;
	const StringName *key = (const StringName *)p_key;
	const Variant *value = (const Variant *)p_value;

	bool valid;
	self->set_named(*key, *value, valid);
	*r_valid = valid;
}

static void foundry_extension_variant_set_keyed(FoundryExtensionVariantPtr p_self, FoundryExtensionConstVariantPtr p_key, FoundryExtensionConstVariantPtr p_value, FoundryExtensionBool *r_valid) {
	Variant *self = (Variant *)p_self;
	const Variant *key = (const Variant *)p_key;
	const Variant *value = (const Variant *)p_value;

	bool valid;
	self->set_keyed(*key, *value, valid);
	*r_valid = valid;
}

static void foundry_extension_variant_set_indexed(FoundryExtensionVariantPtr p_self, FoundryExtensionInt p_index, FoundryExtensionConstVariantPtr p_value, FoundryExtensionBool *r_valid, FoundryExtensionBool *r_oob) {
	Variant *self = (Variant *)p_self;
	const Variant *value = (const Variant *)p_value;

	bool valid;
	bool oob;
	self->set_indexed(p_index, *value, valid, oob);
	*r_valid = valid;
	*r_oob = oob;
}

static void foundry_extension_variant_get(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstVariantPtr p_key, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	const Variant *key = (const Variant *)p_key;

	bool valid;
	memnew_placement(r_ret, Variant(self->get(*key, &valid)));
	*r_valid = valid;
}

static void foundry_extension_variant_get_named(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstStringNamePtr p_key, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	const StringName *key = (const StringName *)p_key;

	bool valid;
	memnew_placement(r_ret, Variant(self->get_named(*key, valid)));
	*r_valid = valid;
}

static void foundry_extension_variant_get_keyed(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstVariantPtr p_key, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	const Variant *key = (const Variant *)p_key;

	bool valid;
	memnew_placement(r_ret, Variant(self->get_keyed(*key, valid)));
	*r_valid = valid;
}

static void foundry_extension_variant_get_indexed(FoundryExtensionConstVariantPtr p_self, FoundryExtensionInt p_index, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool *r_valid, FoundryExtensionBool *r_oob) {
	const Variant *self = (const Variant *)p_self;

	bool valid;
	bool oob;
	memnew_placement(r_ret, Variant(self->get_indexed(p_index, valid, oob)));
	*r_valid = valid;
	*r_oob = oob;
}

/// Iteration.
static FoundryExtensionBool foundry_extension_variant_iter_init(FoundryExtensionConstVariantPtr p_self, FoundryExtensionUninitializedVariantPtr r_iter, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	memnew_placement(r_iter, Variant);
	Variant *iter = reinterpret_cast<Variant *>(r_iter);

	bool valid;
	bool ret = self->iter_init(*iter, valid);
	*r_valid = valid;
	return ret;
}

static FoundryExtensionBool foundry_extension_variant_iter_next(FoundryExtensionConstVariantPtr p_self, FoundryExtensionVariantPtr r_iter, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	Variant *iter = (Variant *)r_iter;

	bool valid;
	bool ret = self->iter_next(*iter, valid);
	*r_valid = valid;
	return ret;
}

static void foundry_extension_variant_iter_get(FoundryExtensionConstVariantPtr p_self, FoundryExtensionVariantPtr r_iter, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	Variant *iter = (Variant *)r_iter;

	bool valid;
	memnew_placement(r_ret, Variant(self->iter_get(*iter, valid)));
	*r_valid = valid;
}

/// Variant functions.
static FoundryExtensionInt foundry_extension_variant_hash(FoundryExtensionConstVariantPtr p_self) {
	const Variant *self = (const Variant *)p_self;
	return self->hash();
}

static FoundryExtensionInt foundry_extension_variant_recursive_hash(FoundryExtensionConstVariantPtr p_self, FoundryExtensionInt p_recursion_count) {
	const Variant *self = (const Variant *)p_self;
	return self->recursive_hash(p_recursion_count);
}

static FoundryExtensionBool foundry_extension_variant_hash_compare(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstVariantPtr p_other) {
	const Variant *self = (const Variant *)p_self;
	const Variant *other = (const Variant *)p_other;
	return self->hash_compare(*other);
}

static FoundryExtensionBool foundry_extension_variant_booleanize(FoundryExtensionConstVariantPtr p_self) {
	const Variant *self = (const Variant *)p_self;
	return self->booleanize();
}

static void foundry_extension_variant_duplicate(FoundryExtensionConstVariantPtr p_self, FoundryExtensionUninitializedVariantPtr r_ret, FoundryExtensionBool p_deep) {
	const Variant *self = (const Variant *)p_self;
	memnew_placement(r_ret, Variant(self->duplicate(p_deep)));
}

static void foundry_extension_variant_stringify(FoundryExtensionConstVariantPtr p_self, FoundryExtensionUninitializedVariantPtr r_ret) {
	const Variant *self = (const Variant *)p_self;
	memnew_placement(r_ret, String(*self));
}

static FoundryExtensionVariantType foundry_extension_variant_get_type(FoundryExtensionConstVariantPtr p_self) {
	const Variant *self = (const Variant *)p_self;
	return (FoundryExtensionVariantType)self->get_type();
}

static FoundryExtensionBool foundry_extension_variant_has_method(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstStringNamePtr p_method) {
	const Variant *self = (const Variant *)p_self;
	const StringName *method = (const StringName *)p_method;
	return self->has_method(*method);
}

static FoundryExtensionBool foundry_extension_variant_has_member(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_member) {
	return Variant::has_member((Variant::Type)p_type, *((const StringName *)p_member));
}

static FoundryExtensionBool foundry_extension_variant_has_key(FoundryExtensionConstVariantPtr p_self, FoundryExtensionConstVariantPtr p_key, FoundryExtensionBool *r_valid) {
	const Variant *self = (const Variant *)p_self;
	const Variant *key = (const Variant *)p_key;
	bool valid;
	bool ret = self->has_key(*key, valid);
	*r_valid = valid;
	return ret;
}

static GDObjectInstanceID foundry_extension_variant_get_object_instance_id(FoundryExtensionConstVariantPtr p_self) {
	const Variant *self = (const Variant *)p_self;
	if (likely(self->get_type() == Variant::OBJECT)) {
		return self->operator ObjectID();
	}
	return 0;
}

static void foundry_extension_variant_get_type_name(FoundryExtensionVariantType p_type, FoundryExtensionUninitializedVariantPtr r_ret) {
	String name = Variant::get_type_name((Variant::Type)p_type);
	memnew_placement(r_ret, String(name));
}

static FoundryExtensionBool foundry_extension_variant_can_convert(FoundryExtensionVariantType p_from, FoundryExtensionVariantType p_to) {
	return Variant::can_convert((Variant::Type)p_from, (Variant::Type)p_to);
}

static FoundryExtensionBool foundry_extension_variant_can_convert_strict(FoundryExtensionVariantType p_from, FoundryExtensionVariantType p_to) {
	return Variant::can_convert_strict((Variant::Type)p_from, (Variant::Type)p_to);
}

// Variant interaction.
static FoundryExtensionVariantFromTypeConstructorFunc foundry_extension_get_variant_from_type_constructor(FoundryExtensionVariantType p_type) {
	switch (p_type) {
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BOOL:
			return VariantTypeConstructor<bool>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_INT:
			return VariantTypeConstructor<int64_t>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_FLOAT:
			return VariantTypeConstructor<double>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING:
			return VariantTypeConstructor<String>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2:
			return VariantTypeConstructor<Vector2>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2I:
			return VariantTypeConstructor<Vector2i>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2:
			return VariantTypeConstructor<Rect2>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2I:
			return VariantTypeConstructor<Rect2i>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3:
			return VariantTypeConstructor<Vector3>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3I:
			return VariantTypeConstructor<Vector3i>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM2D:
			return VariantTypeConstructor<Transform2D>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4:
			return VariantTypeConstructor<Vector4>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4I:
			return VariantTypeConstructor<Vector4i>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PLANE:
			return VariantTypeConstructor<Plane>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_QUATERNION:
			return VariantTypeConstructor<Quaternion>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_AABB:
			return VariantTypeConstructor<AABB>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BASIS:
			return VariantTypeConstructor<Basis>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM3D:
			return VariantTypeConstructor<Transform3D>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PROJECTION:
			return VariantTypeConstructor<Projection>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_COLOR:
			return VariantTypeConstructor<Color>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING_NAME:
			return VariantTypeConstructor<StringName>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NODE_PATH:
			return VariantTypeConstructor<NodePath>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RID:
			return VariantTypeConstructor<RID>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_OBJECT:
			return VariantTypeConstructor<Object *>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_CALLABLE:
			return VariantTypeConstructor<Callable>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_SIGNAL:
			return VariantTypeConstructor<Signal>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_DICTIONARY:
			return VariantTypeConstructor<Dictionary>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_ARRAY:
			return VariantTypeConstructor<Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY:
			return VariantTypeConstructor<PackedByteArray>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
			return VariantTypeConstructor<PackedInt32Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
			return VariantTypeConstructor<PackedInt64Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
			return VariantTypeConstructor<PackedFloat32Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
			return VariantTypeConstructor<PackedFloat64Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
			return VariantTypeConstructor<PackedStringArray>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
			return VariantTypeConstructor<PackedVector2Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
			return VariantTypeConstructor<PackedVector3Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR4_ARRAY:
			return VariantTypeConstructor<PackedVector4Array>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY:
			return VariantTypeConstructor<PackedColorArray>::variant_from_type;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NIL:
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VARIANT_MAX:
			ERR_FAIL_V_MSG(nullptr, "Getting Variant conversion function with invalid type");
	}
	ERR_FAIL_V_MSG(nullptr, "Getting Variant conversion function with invalid type");
}

static FoundryExtensionTypeFromVariantConstructorFunc foundry_extension_get_variant_to_type_constructor(FoundryExtensionVariantType p_type) {
	switch (p_type) {
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BOOL:
			return VariantTypeConstructor<bool>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_INT:
			return VariantTypeConstructor<int64_t>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_FLOAT:
			return VariantTypeConstructor<double>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING:
			return VariantTypeConstructor<String>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2:
			return VariantTypeConstructor<Vector2>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2I:
			return VariantTypeConstructor<Vector2i>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2:
			return VariantTypeConstructor<Rect2>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2I:
			return VariantTypeConstructor<Rect2i>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3:
			return VariantTypeConstructor<Vector3>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3I:
			return VariantTypeConstructor<Vector3i>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM2D:
			return VariantTypeConstructor<Transform2D>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4:
			return VariantTypeConstructor<Vector4>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4I:
			return VariantTypeConstructor<Vector4i>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PLANE:
			return VariantTypeConstructor<Plane>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_QUATERNION:
			return VariantTypeConstructor<Quaternion>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_AABB:
			return VariantTypeConstructor<AABB>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BASIS:
			return VariantTypeConstructor<Basis>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM3D:
			return VariantTypeConstructor<Transform3D>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PROJECTION:
			return VariantTypeConstructor<Projection>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_COLOR:
			return VariantTypeConstructor<Color>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING_NAME:
			return VariantTypeConstructor<StringName>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NODE_PATH:
			return VariantTypeConstructor<NodePath>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RID:
			return VariantTypeConstructor<RID>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_OBJECT:
			return VariantTypeConstructor<Object *>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_CALLABLE:
			return VariantTypeConstructor<Callable>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_SIGNAL:
			return VariantTypeConstructor<Signal>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_DICTIONARY:
			return VariantTypeConstructor<Dictionary>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_ARRAY:
			return VariantTypeConstructor<Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY:
			return VariantTypeConstructor<PackedByteArray>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
			return VariantTypeConstructor<PackedInt32Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
			return VariantTypeConstructor<PackedInt64Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
			return VariantTypeConstructor<PackedFloat32Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
			return VariantTypeConstructor<PackedFloat64Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
			return VariantTypeConstructor<PackedStringArray>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
			return VariantTypeConstructor<PackedVector2Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
			return VariantTypeConstructor<PackedVector3Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR4_ARRAY:
			return VariantTypeConstructor<PackedVector4Array>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY:
			return VariantTypeConstructor<PackedColorArray>::type_from_variant;
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NIL:
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VARIANT_MAX:
			ERR_FAIL_V_MSG(nullptr, "Getting Variant conversion function with invalid type");
	}
	ERR_FAIL_V_MSG(nullptr, "Getting Variant conversion function with invalid type");
}

static FoundryExtensionVariantGetInternalPtrFunc foundry_extension_variant_get_ptr_internal_getter(FoundryExtensionVariantType p_type) {
	switch (p_type) {
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BOOL:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<bool *(*)(Variant *)>(VariantInternal::get_bool));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_INT:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<int64_t *(*)(Variant *)>(VariantInternal::get_int));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_FLOAT:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<double *(*)(Variant *)>(VariantInternal::get_float));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<String *(*)(Variant *)>(VariantInternal::get_string));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector2 *(*)(Variant *)>(VariantInternal::get_vector2));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR2I:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector2i *(*)(Variant *)>(VariantInternal::get_vector2i));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Rect2 *(*)(Variant *)>(VariantInternal::get_rect2));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RECT2I:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Rect2i *(*)(Variant *)>(VariantInternal::get_rect2i));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector3 *(*)(Variant *)>(VariantInternal::get_vector3));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR3I:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector3i *(*)(Variant *)>(VariantInternal::get_vector3i));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM2D:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Transform2D *(*)(Variant *)>(VariantInternal::get_transform2d));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector4 *(*)(Variant *)>(VariantInternal::get_vector4));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VECTOR4I:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Vector4i *(*)(Variant *)>(VariantInternal::get_vector4i));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PLANE:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Plane *(*)(Variant *)>(VariantInternal::get_plane));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_QUATERNION:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Quaternion *(*)(Variant *)>(VariantInternal::get_quaternion));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_AABB:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<AABB *(*)(Variant *)>(VariantInternal::get_aabb));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_BASIS:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Basis *(*)(Variant *)>(VariantInternal::get_basis));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_TRANSFORM3D:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Transform3D *(*)(Variant *)>(VariantInternal::get_transform));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PROJECTION:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Projection *(*)(Variant *)>(VariantInternal::get_projection));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_COLOR:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Color *(*)(Variant *)>(VariantInternal::get_color));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_STRING_NAME:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<StringName *(*)(Variant *)>(VariantInternal::get_string_name));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NODE_PATH:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<NodePath *(*)(Variant *)>(VariantInternal::get_node_path));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_RID:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<RID *(*)(Variant *)>(VariantInternal::get_rid));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_OBJECT:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Object **(*)(Variant *)>(VariantInternal::get_object));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_CALLABLE:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Callable *(*)(Variant *)>(VariantInternal::get_callable));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_SIGNAL:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Signal *(*)(Variant *)>(VariantInternal::get_signal));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_DICTIONARY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Dictionary *(*)(Variant *)>(VariantInternal::get_dictionary));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<Array *(*)(Variant *)>(VariantInternal::get_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedByteArray *(*)(Variant *)>(VariantInternal::get_byte_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedInt32Array *(*)(Variant *)>(VariantInternal::get_int32_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedInt64Array *(*)(Variant *)>(VariantInternal::get_int64_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedFloat32Array *(*)(Variant *)>(VariantInternal::get_float32_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedFloat64Array *(*)(Variant *)>(VariantInternal::get_float64_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedStringArray *(*)(Variant *)>(VariantInternal::get_string_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedVector2Array *(*)(Variant *)>(VariantInternal::get_vector2_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedVector3Array *(*)(Variant *)>(VariantInternal::get_vector3_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedColorArray *(*)(Variant *)>(VariantInternal::get_color_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_PACKED_VECTOR4_ARRAY:
			return reinterpret_cast<FoundryExtensionVariantGetInternalPtrFunc>(static_cast<PackedVector4Array *(*)(Variant *)>(VariantInternal::get_vector4_array));
		case FOUNDRY_EXTENSION_VARIANT_TYPE_NIL:
		case FOUNDRY_EXTENSION_VARIANT_TYPE_VARIANT_MAX:
			ERR_FAIL_V_MSG(nullptr, "Getting Variant get internal pointer function with invalid type.");
	}
	ERR_FAIL_V_MSG(nullptr, "Getting Variant get internal pointer function with invalid type.");
}

// ptrcalls
static FoundryExtensionPtrOperatorEvaluator foundry_extension_variant_get_ptr_operator_evaluator(FoundryExtensionVariantOperator p_operator, FoundryExtensionVariantType p_type_a, FoundryExtensionVariantType p_type_b) {
	return (FoundryExtensionPtrOperatorEvaluator)Variant::get_ptr_operator_evaluator(Variant::Operator(p_operator), Variant::Type(p_type_a), Variant::Type(p_type_b));
}
static FoundryExtensionPtrBuiltInMethod foundry_extension_variant_get_ptr_builtin_method(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_method, FoundryExtensionInt p_hash) {
	const StringName method = *reinterpret_cast<const StringName *>(p_method);
	FoundryExtensionPtrBuiltInMethod ptr = (FoundryExtensionPtrBuiltInMethod)Variant::get_ptr_builtin_method_with_compatibility(Variant::Type(p_type), method, p_hash);
	if (!ptr) {
		ERR_PRINT("Error getting method " + method + ", missing or hash mismatch.");
	}
	return ptr;
}
static FoundryExtensionPtrConstructor foundry_extension_variant_get_ptr_constructor(FoundryExtensionVariantType p_type, int32_t p_constructor) {
	return (FoundryExtensionPtrConstructor)Variant::get_ptr_constructor(Variant::Type(p_type), p_constructor);
}
static FoundryExtensionPtrDestructor foundry_extension_variant_get_ptr_destructor(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrDestructor)Variant::get_ptr_destructor(Variant::Type(p_type));
}
static void foundry_extension_variant_construct(FoundryExtensionVariantType p_type, FoundryExtensionUninitializedVariantPtr r_base, const FoundryExtensionConstVariantPtr *p_args, int32_t p_argument_count, FoundryExtensionCallError *r_error) {
	memnew_placement(r_base, Variant);
	Variant *base = reinterpret_cast<Variant *>(r_base);

	Callable::CallError error;
	Variant::construct(Variant::Type(p_type), *base, (const Variant **)p_args, p_argument_count, error);

	if (r_error) {
		r_error->error = (FoundryExtensionCallErrorType)(error.error);
		r_error->argument = error.argument;
		r_error->expected = error.expected;
	}
}
static FoundryExtensionPtrSetter foundry_extension_variant_get_ptr_setter(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_member) {
	const StringName member = *reinterpret_cast<const StringName *>(p_member);
	return (FoundryExtensionPtrSetter)Variant::get_member_ptr_setter(Variant::Type(p_type), member);
}
static FoundryExtensionPtrGetter foundry_extension_variant_get_ptr_getter(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_member) {
	const StringName member = *reinterpret_cast<const StringName *>(p_member);
	return (FoundryExtensionPtrGetter)Variant::get_member_ptr_getter(Variant::Type(p_type), member);
}
static FoundryExtensionPtrIndexedSetter foundry_extension_variant_get_ptr_indexed_setter(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrIndexedSetter)Variant::get_member_ptr_indexed_setter(Variant::Type(p_type));
}
static FoundryExtensionPtrIndexedGetter foundry_extension_variant_get_ptr_indexed_getter(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrIndexedGetter)Variant::get_member_ptr_indexed_getter(Variant::Type(p_type));
}
static FoundryExtensionPtrKeyedSetter foundry_extension_variant_get_ptr_keyed_setter(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrKeyedSetter)Variant::get_member_ptr_keyed_setter(Variant::Type(p_type));
}
static FoundryExtensionPtrKeyedGetter foundry_extension_variant_get_ptr_keyed_getter(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrKeyedGetter)Variant::get_member_ptr_keyed_getter(Variant::Type(p_type));
}
static FoundryExtensionPtrKeyedChecker foundry_extension_variant_get_ptr_keyed_checker(FoundryExtensionVariantType p_type) {
	return (FoundryExtensionPtrKeyedChecker)Variant::get_member_ptr_keyed_checker(Variant::Type(p_type));
}
static void foundry_extension_variant_get_constant_value(FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_constant, FoundryExtensionUninitializedVariantPtr r_ret) {
	StringName constant = *reinterpret_cast<const StringName *>(p_constant);
	memnew_placement(r_ret, Variant(Variant::get_constant_value(Variant::Type(p_type), constant)));
}
static FoundryExtensionPtrUtilityFunction foundry_extension_variant_get_ptr_utility_function(FoundryExtensionConstStringNamePtr p_function, FoundryExtensionInt p_hash) {
	StringName function = *reinterpret_cast<const StringName *>(p_function);
	uint32_t hash = Variant::get_utility_function_hash(function);
	if (hash != p_hash) {
		ERR_PRINT_ONCE("Error getting utility function " + function + ", hash mismatch.");
		return nullptr;
	}
	return (FoundryExtensionPtrUtilityFunction)Variant::get_ptr_utility_function(function);
}

//string helpers

static void foundry_extension_string_new_with_latin1_chars(FoundryExtensionUninitializedStringPtr r_dest, const char *p_contents) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_latin1(Span<char>(p_contents, p_contents ? strlen(p_contents) : 0));
}

static void foundry_extension_string_new_with_utf8_chars(FoundryExtensionUninitializedStringPtr r_dest, const char *p_contents) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_utf8(p_contents);
}

static void foundry_extension_string_new_with_utf16_chars(FoundryExtensionUninitializedStringPtr r_dest, const char16_t *p_contents) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_utf16(p_contents);
}

static void foundry_extension_string_new_with_utf32_chars(FoundryExtensionUninitializedStringPtr r_dest, const char32_t *p_contents) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_utf32(Span(p_contents, p_contents ? strlen(p_contents) : 0));
}

static void foundry_extension_string_new_with_wide_chars(FoundryExtensionUninitializedStringPtr r_dest, const wchar_t *p_contents) {
	if constexpr (sizeof(wchar_t) == 2) {
		// wchar_t is 16 bit (UTF-16).
		String *dest = memnew_placement(r_dest, String);
		dest->append_utf16((const char16_t *)p_contents);
	} else {
		// wchar_t is 32 bit (UTF-32).
		String *string = memnew_placement(r_dest, String);
		string->append_utf32(Span((const char32_t *)p_contents, p_contents ? strlen(p_contents) : 0));
	}
}

static void foundry_extension_string_new_with_latin1_chars_and_len(FoundryExtensionUninitializedStringPtr r_dest, const char *p_contents, FoundryExtensionInt p_size) {
	const size_t string_length = p_contents ? (p_size < 0 ? strlen(p_contents) : strnlen(p_contents, p_size)) : 0;
	String *dest = memnew_placement(r_dest, String);
	dest->append_latin1(Span(p_contents, string_length));
}

static void foundry_extension_string_new_with_utf8_chars_and_len(FoundryExtensionUninitializedStringPtr r_dest, const char *p_contents, FoundryExtensionInt p_size) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_utf8(p_contents, p_size);
}

static FoundryExtensionInt foundry_extension_string_new_with_utf8_chars_and_len2(FoundryExtensionUninitializedStringPtr r_dest, const char *p_contents, FoundryExtensionInt p_size) {
	String *dest = memnew_placement(r_dest, String);
	return (FoundryExtensionInt)dest->append_utf8(p_contents, p_size);
}

static void foundry_extension_string_new_with_utf16_chars_and_len(FoundryExtensionUninitializedStringPtr r_dest, const char16_t *p_contents, FoundryExtensionInt p_char_count) {
	String *dest = memnew_placement(r_dest, String);
	dest->append_utf16(p_contents, p_char_count);
}

static FoundryExtensionInt foundry_extension_string_new_with_utf16_chars_and_len2(FoundryExtensionUninitializedStringPtr r_dest, const char16_t *p_contents, FoundryExtensionInt p_char_count, FoundryExtensionBool p_default_little_endian) {
	String *dest = memnew_placement(r_dest, String);
	return (FoundryExtensionInt)dest->append_utf16(p_contents, p_char_count, p_default_little_endian);
}

static void foundry_extension_string_new_with_utf32_chars_and_len(FoundryExtensionUninitializedStringPtr r_dest, const char32_t *p_contents, FoundryExtensionInt p_char_count) {
	const size_t string_length = p_contents ? (p_char_count < 0 ? strlen(p_contents) : strnlen(p_contents, p_char_count)) : 0;
	String *string = memnew_placement(r_dest, String);
	string->append_utf32(Span(p_contents, string_length));
}

static void foundry_extension_string_new_with_wide_chars_and_len(FoundryExtensionUninitializedStringPtr r_dest, const wchar_t *p_contents, FoundryExtensionInt p_char_count) {
	if constexpr (sizeof(wchar_t) == 2) {
		// wchar_t is 16 bit (UTF-16).
		String *dest = memnew_placement(r_dest, String);
		dest->append_utf16((const char16_t *)p_contents, p_char_count);
	} else {
		// wchar_t is 32 bit (UTF-32).
		const size_t string_length = p_contents ? (p_char_count < 0 ? strlen(p_contents) : strnlen((const char32_t *)p_contents, p_char_count)) : 0;
		String *string = memnew_placement(r_dest, String);
		string->append_utf32(Span((const char32_t *)p_contents, string_length));
	}
}

static FoundryExtensionInt foundry_extension_string_to_latin1_chars(FoundryExtensionConstStringPtr p_self, char *r_text, FoundryExtensionInt p_max_write_length) {
	String *self = (String *)p_self;
	CharString cs = self->latin1();
	FoundryExtensionInt len = cs.length();
	if (r_text) {
		const char *s_text = cs.ptr();
		for (FoundryExtensionInt i = 0; i < MIN(len, p_max_write_length); i++) {
			r_text[i] = s_text[i];
		}
	}
	return len;
}
static FoundryExtensionInt foundry_extension_string_to_utf8_chars(FoundryExtensionConstStringPtr p_self, char *r_text, FoundryExtensionInt p_max_write_length) {
	String *self = (String *)p_self;
	CharString cs = self->utf8();
	FoundryExtensionInt len = cs.length();
	if (r_text) {
		const char *s_text = cs.ptr();
		for (FoundryExtensionInt i = 0; i < MIN(len, p_max_write_length); i++) {
			r_text[i] = s_text[i];
		}
	}
	return len;
}
static FoundryExtensionInt foundry_extension_string_to_utf16_chars(FoundryExtensionConstStringPtr p_self, char16_t *r_text, FoundryExtensionInt p_max_write_length) {
	String *self = (String *)p_self;
	Char16String cs = self->utf16();
	FoundryExtensionInt len = cs.length();
	if (r_text) {
		const char16_t *s_text = cs.ptr();
		for (FoundryExtensionInt i = 0; i < MIN(len, p_max_write_length); i++) {
			r_text[i] = s_text[i];
		}
	}
	return len;
}
static FoundryExtensionInt foundry_extension_string_to_utf32_chars(FoundryExtensionConstStringPtr p_self, char32_t *r_text, FoundryExtensionInt p_max_write_length) {
	String *self = (String *)p_self;
	FoundryExtensionInt len = self->length();
	if (r_text) {
		const char32_t *s_text = self->ptr();
		for (FoundryExtensionInt i = 0; i < MIN(len, p_max_write_length); i++) {
			r_text[i] = s_text[i];
		}
	}
	return len;
}
static FoundryExtensionInt foundry_extension_string_to_wide_chars(FoundryExtensionConstStringPtr p_self, wchar_t *r_text, FoundryExtensionInt p_max_write_length) {
	if constexpr (sizeof(wchar_t) == 4) {
		return foundry_extension_string_to_utf32_chars(p_self, (char32_t *)r_text, p_max_write_length);
	} else {
		return foundry_extension_string_to_utf16_chars(p_self, (char16_t *)r_text, p_max_write_length);
	}
}

static char32_t *foundry_extension_string_operator_index(FoundryExtensionStringPtr p_self, FoundryExtensionInt p_index) {
	String *self = (String *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->length() + 1)) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const char32_t *foundry_extension_string_operator_index_const(FoundryExtensionConstStringPtr p_self, FoundryExtensionInt p_index) {
	const String *self = (const String *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->length() + 1)) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static void foundry_extension_string_operator_plus_eq_string(FoundryExtensionStringPtr p_self, FoundryExtensionConstStringPtr p_b) {
	String *self = (String *)p_self;
	const String *b = (const String *)p_b;
	*self += *b;
}

static void foundry_extension_string_operator_plus_eq_char(FoundryExtensionStringPtr p_self, char32_t p_b) {
	String *self = (String *)p_self;
	*self += p_b;
}

static void foundry_extension_string_operator_plus_eq_cstr(FoundryExtensionStringPtr p_self, const char *p_b) {
	String *self = (String *)p_self;
	*self += p_b;
}

static void foundry_extension_string_operator_plus_eq_wcstr(FoundryExtensionStringPtr p_self, const wchar_t *p_b) {
	String *self = (String *)p_self;
	*self += p_b;
}

static void foundry_extension_string_operator_plus_eq_c32str(FoundryExtensionStringPtr p_self, const char32_t *p_b) {
	String *self = (String *)p_self;
	*self += p_b;
}

static FoundryExtensionInt foundry_extension_string_resize(FoundryExtensionStringPtr p_self, FoundryExtensionInt p_length) {
	String *self = (String *)p_self;
	return (*self).resize_uninitialized(p_length);
}

static void foundry_extension_string_name_new_with_latin1_chars(FoundryExtensionUninitializedStringNamePtr r_dest, const char *p_contents, FoundryExtensionBool p_is_static) {
	memnew_placement(r_dest, StringName(p_contents, static_cast<bool>(p_is_static)));
}

static void foundry_extension_string_name_new_with_utf8_chars(FoundryExtensionUninitializedStringNamePtr r_dest, const char *p_contents) {
	String tmp = String::utf8(p_contents);
	memnew_placement(r_dest, StringName(tmp));
}

static void foundry_extension_string_name_new_with_utf8_chars_and_len(FoundryExtensionUninitializedStringNamePtr r_dest, const char *p_contents, FoundryExtensionInt p_size) {
	String tmp = String::utf8(p_contents, p_size);
	memnew_placement(r_dest, StringName(tmp));
}

static FoundryExtensionInt foundry_extension_xml_parser_open_buffer(FoundryExtensionObjectPtr p_instance, const uint8_t *p_buffer, size_t p_size) {
	XMLParser *xml = (XMLParser *)p_instance;
	return (FoundryExtensionInt)xml->_open_buffer(p_buffer, p_size);
}

static void foundry_extension_file_access_store_buffer(FoundryExtensionObjectPtr p_instance, const uint8_t *p_src, uint64_t p_length) {
	FileAccess *fa = (FileAccess *)p_instance;
	fa->store_buffer(p_src, p_length);
}

static uint64_t foundry_extension_file_access_get_buffer(FoundryExtensionConstObjectPtr p_instance, uint8_t *p_dst, uint64_t p_length) {
	const FileAccess *fa = (FileAccess *)p_instance;
	return fa->get_buffer(p_dst, p_length);
}

static uint8_t *foundry_extension_image_ptrw(FoundryExtensionObjectPtr p_instance) {
	Image *img = (Image *)p_instance;
	return img->ptrw();
}

static const uint8_t *foundry_extension_image_ptr(FoundryExtensionObjectPtr p_instance) {
	Image *img = (Image *)p_instance;
	return img->ptr();
}

static int64_t foundry_extension_worker_thread_pool_add_native_group_task(FoundryExtensionObjectPtr p_instance, void (*p_func)(void *, uint32_t), void *p_userdata, int p_elements, int p_tasks, FoundryExtensionBool p_high_priority, FoundryExtensionConstStringPtr p_description) {
	WorkerThreadPool *p = (WorkerThreadPool *)p_instance;
	const String *description = (const String *)p_description;
	return (int64_t)p->add_native_group_task(p_func, p_userdata, p_elements, p_tasks, static_cast<bool>(p_high_priority), *description);
}

static int64_t foundry_extension_worker_thread_pool_add_native_task(FoundryExtensionObjectPtr p_instance, void (*p_func)(void *), void *p_userdata, FoundryExtensionBool p_high_priority, FoundryExtensionConstStringPtr p_description) {
	WorkerThreadPool *p = (WorkerThreadPool *)p_instance;
	const String *description = (const String *)p_description;
	return (int64_t)p->add_native_task(p_func, p_userdata, static_cast<bool>(p_high_priority), *description);
}

/* Packed array functions */

static uint8_t *foundry_extension_packed_byte_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedByteArray *self = (PackedByteArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const uint8_t *foundry_extension_packed_byte_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedByteArray *self = (const PackedByteArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_color_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedColorArray *self = (PackedColorArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptrw()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_color_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedColorArray *self = (const PackedColorArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptr()[p_index];
}

static float *foundry_extension_packed_float32_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedFloat32Array *self = (PackedFloat32Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const float *foundry_extension_packed_float32_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedFloat32Array *self = (const PackedFloat32Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static double *foundry_extension_packed_float64_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedFloat64Array *self = (PackedFloat64Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const double *foundry_extension_packed_float64_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedFloat64Array *self = (const PackedFloat64Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static int32_t *foundry_extension_packed_int32_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedInt32Array *self = (PackedInt32Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const int32_t *foundry_extension_packed_int32_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedInt32Array *self = (const PackedInt32Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static int64_t *foundry_extension_packed_int64_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedInt64Array *self = (PackedInt64Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptrw()[p_index];
}

static const int64_t *foundry_extension_packed_int64_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedInt64Array *self = (const PackedInt64Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return &self->ptr()[p_index];
}

static FoundryExtensionStringPtr foundry_extension_packed_string_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedStringArray *self = (PackedStringArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionStringPtr)&self->ptrw()[p_index];
}

static FoundryExtensionStringPtr foundry_extension_packed_string_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedStringArray *self = (const PackedStringArray *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionStringPtr)&self->ptr()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector2_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedVector2Array *self = (PackedVector2Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptrw()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector2_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedVector2Array *self = (const PackedVector2Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptr()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector3_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedVector3Array *self = (PackedVector3Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptrw()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector3_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedVector3Array *self = (const PackedVector3Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptr()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector4_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	PackedVector4Array *self = (PackedVector4Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptrw()[p_index];
}

static FoundryExtensionTypePtr foundry_extension_packed_vector4_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const PackedVector4Array *self = (const PackedVector4Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionTypePtr)&self->ptr()[p_index];
}

static FoundryExtensionVariantPtr foundry_extension_array_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionInt p_index) {
	Array *self = (Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionVariantPtr)&self->operator[](p_index);
}

static FoundryExtensionVariantPtr foundry_extension_array_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionInt p_index) {
	const Array *self = (const Array *)p_self;
	if (unlikely(p_index < 0 || p_index >= self->size())) {
		return nullptr;
	}
	return (FoundryExtensionVariantPtr)&self->operator[](p_index);
}

#ifndef DISABLE_DEPRECATED
void foundry_extension_array_ref(FoundryExtensionTypePtr p_self, FoundryExtensionConstTypePtr p_from) {
	Array *self = (Array *)p_self;
	const Array *from = (const Array *)p_from;
	self->Array::operator=(*from);
}
#endif // DISABLE_DEPRECATED

void foundry_extension_array_set_typed(FoundryExtensionTypePtr p_self, FoundryExtensionVariantType p_type, FoundryExtensionConstStringNamePtr p_class_name, FoundryExtensionConstVariantPtr p_script) {
	Array *self = reinterpret_cast<Array *>(p_self);
	const StringName *class_name = reinterpret_cast<const StringName *>(p_class_name);
	const Variant *script = reinterpret_cast<const Variant *>(p_script);
	self->set_typed((uint32_t)p_type, *class_name, *script);
}

static FoundryExtensionBool foundry_extension_array_set_typed_by_descriptor(FoundryExtensionTypePtr p_self, FoundryExtensionConstVariantPtr p_element_type_descriptor) {
	Array *self = reinterpret_cast<Array *>(p_self);
	const Variant *element_type_descriptor = reinterpret_cast<const Variant *>(p_element_type_descriptor);
	ContainerType element_type;
	String error;
	if (!ContainerTypeDescriptor::from_variant(*element_type_descriptor, element_type, &error)) {
		ERR_PRINT(vformat("Invalid Array element type descriptor: %s", error));
		return false;
	}
	self->set_typed(element_type);
	return true;
}

static void foundry_extension_array_get_typed_element_type_descriptor(FoundryExtensionConstTypePtr p_self, FoundryExtensionUninitializedVariantPtr r_element_type_descriptor) {
	const Array *self = reinterpret_cast<const Array *>(p_self);
	memnew_placement(r_element_type_descriptor, Variant(ContainerTypeDescriptor::to_variant(self->get_element_type())));
}

/* Dictionary functions */

static FoundryExtensionVariantPtr foundry_extension_dictionary_operator_index(FoundryExtensionTypePtr p_self, FoundryExtensionConstVariantPtr p_key) {
	Dictionary *self = (Dictionary *)p_self;
	return (FoundryExtensionVariantPtr)&self->operator[](*(const Variant *)p_key);
}

static FoundryExtensionVariantPtr foundry_extension_dictionary_operator_index_const(FoundryExtensionConstTypePtr p_self, FoundryExtensionConstVariantPtr p_key) {
	const Dictionary *self = (const Dictionary *)p_self;
	return (FoundryExtensionVariantPtr)&self->operator[](*(const Variant *)p_key);
}

void foundry_extension_dictionary_set_typed(FoundryExtensionTypePtr p_self, FoundryExtensionVariantType p_key_type, FoundryExtensionConstStringNamePtr p_key_class_name, FoundryExtensionConstVariantPtr p_key_script, FoundryExtensionVariantType p_value_type, FoundryExtensionConstStringNamePtr p_value_class_name, FoundryExtensionConstVariantPtr p_value_script) {
	Dictionary *self = reinterpret_cast<Dictionary *>(p_self);
	const StringName *key_class_name = reinterpret_cast<const StringName *>(p_key_class_name);
	const Variant *key_script = reinterpret_cast<const Variant *>(p_key_script);
	const StringName *value_class_name = reinterpret_cast<const StringName *>(p_value_class_name);
	const Variant *value_script = reinterpret_cast<const Variant *>(p_value_script);
	self->set_typed((uint32_t)p_key_type, *key_class_name, *key_script, (uint32_t)p_value_type, *value_class_name, *value_script);
}

static FoundryExtensionBool foundry_extension_dictionary_set_typed_by_descriptor(FoundryExtensionTypePtr p_self, FoundryExtensionConstVariantPtr p_key_type_descriptor, FoundryExtensionConstVariantPtr p_value_type_descriptor) {
	Dictionary *self = reinterpret_cast<Dictionary *>(p_self);
	const Variant *key_type_descriptor = reinterpret_cast<const Variant *>(p_key_type_descriptor);
	const Variant *value_type_descriptor = reinterpret_cast<const Variant *>(p_value_type_descriptor);

	ContainerType key_type;
	String error;
	if (!ContainerTypeDescriptor::from_variant(*key_type_descriptor, key_type, &error)) {
		ERR_PRINT(vformat("Invalid Dictionary key type descriptor: %s", error));
		return false;
	}

	ContainerType value_type;
	if (!ContainerTypeDescriptor::from_variant(*value_type_descriptor, value_type, &error)) {
		ERR_PRINT(vformat("Invalid Dictionary value type descriptor: %s", error));
		return false;
	}

	self->set_typed(key_type, value_type);
	return true;
}

static void foundry_extension_dictionary_get_typed_key_type_descriptor(FoundryExtensionConstTypePtr p_self, FoundryExtensionUninitializedVariantPtr r_key_type_descriptor) {
	const Dictionary *self = reinterpret_cast<const Dictionary *>(p_self);
	memnew_placement(r_key_type_descriptor, Variant(ContainerTypeDescriptor::to_variant(self->get_key_type())));
}

static void foundry_extension_dictionary_get_typed_value_type_descriptor(FoundryExtensionConstTypePtr p_self, FoundryExtensionUninitializedVariantPtr r_value_type_descriptor) {
	const Dictionary *self = reinterpret_cast<const Dictionary *>(p_self);
	memnew_placement(r_value_type_descriptor, Variant(ContainerTypeDescriptor::to_variant(self->get_value_type())));
}

/* OBJECT API */

static void foundry_extension_object_method_bind_call(FoundryExtensionMethodBindPtr p_method_bind, FoundryExtensionObjectPtr p_instance, const FoundryExtensionConstVariantPtr *p_args, FoundryExtensionInt p_arg_count, FoundryExtensionUninitializedVariantPtr r_return, FoundryExtensionCallError *r_error) {
	const MethodBind *mb = reinterpret_cast<const MethodBind *>(p_method_bind);
	Object *o = (Object *)p_instance;
	const Variant **args = (const Variant **)p_args;
	Callable::CallError error;

	memnew_placement(r_return, Variant(mb->call(o, args, p_arg_count, error)));

	if (r_error) {
		r_error->error = (FoundryExtensionCallErrorType)(error.error);
		r_error->argument = error.argument;
		r_error->expected = error.expected;
	}
}

static void foundry_extension_object_method_bind_ptrcall(FoundryExtensionMethodBindPtr p_method_bind, FoundryExtensionObjectPtr p_instance, const FoundryExtensionConstTypePtr *p_args, FoundryExtensionTypePtr p_ret) {
	const MethodBind *mb = reinterpret_cast<const MethodBind *>(p_method_bind);
	Object *o = (Object *)p_instance;
	mb->ptrcall(o, (const void **)p_args, p_ret);
}

static void foundry_extension_object_destroy(FoundryExtensionObjectPtr p_o) {
	memdelete((Object *)p_o);
}

static FoundryExtensionObjectPtr foundry_extension_global_get_singleton(FoundryExtensionConstStringNamePtr p_name) {
	const StringName name = *reinterpret_cast<const StringName *>(p_name);
	return (FoundryExtensionObjectPtr)Engine::get_singleton()->get_singleton_object(name);
}

static void *foundry_extension_object_get_instance_binding(FoundryExtensionObjectPtr p_object, void *p_token, const FoundryExtensionInstanceBindingCallbacks *p_callbacks) {
	Object *o = (Object *)p_object;
	return o->get_instance_binding(p_token, p_callbacks);
}

static void foundry_extension_object_set_instance_binding(FoundryExtensionObjectPtr p_object, void *p_token, void *p_binding, const FoundryExtensionInstanceBindingCallbacks *p_callbacks) {
	Object *o = (Object *)p_object;
	o->set_instance_binding(p_token, p_binding, p_callbacks);
}

static void foundry_extension_object_free_instance_binding(FoundryExtensionObjectPtr p_object, void *p_token) {
	Object *o = (Object *)p_object;
	o->free_instance_binding(p_token);
}

static void foundry_extension_object_set_instance(FoundryExtensionObjectPtr p_object, FoundryExtensionConstStringNamePtr p_classname, FoundryExtensionClassInstancePtr p_instance) {
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	Object *o = (Object *)p_object;
	ClassDB::set_object_extension_instance(o, classname, p_instance);
}

static FoundryExtensionObjectPtr foundry_extension_object_get_instance_from_id(GDObjectInstanceID p_instance_id) {
	return (FoundryExtensionObjectPtr)ObjectDB::get_instance(ObjectID(p_instance_id));
}

static FoundryExtensionBool foundry_extension_object_get_class_name(FoundryExtensionConstObjectPtr p_object, FoundryExtensionClassLibraryPtr p_library, FoundryExtensionUninitializedStringNamePtr r_class_name) {
	if (!p_object) {
		return false;
	}
	const Object *o = (const Object *)p_object;

	memnew_placement(r_class_name, StringName);
	StringName *class_name = reinterpret_cast<StringName *>(r_class_name);
	*class_name = o->get_class_name_for_extension((FoundryExtension *)p_library);

	return true;
}

static FoundryExtensionObjectPtr foundry_extension_object_cast_to(FoundryExtensionConstObjectPtr p_object, void *p_class_tag) {
	if (!p_object) {
		return nullptr;
	}
	Object *o = (Object *)p_object;

	return o->is_class_ptr(p_class_tag) ? (FoundryExtensionObjectPtr)o : (FoundryExtensionObjectPtr) nullptr;
}

static GDObjectInstanceID foundry_extension_object_get_instance_id(FoundryExtensionConstObjectPtr p_object) {
	const Object *o = (const Object *)p_object;
	return (GDObjectInstanceID)o->get_instance_id();
}

static FoundryExtensionBool foundry_extension_object_has_script_method(FoundryExtensionConstObjectPtr p_object, FoundryExtensionConstStringNamePtr p_method) {
	Object *o = (Object *)p_object;
	const StringName method = *reinterpret_cast<const StringName *>(p_method);

	ScriptInstance *script_instance = o->get_script_instance();
	if (script_instance) {
		return script_instance->has_method(method);
	}
	return false;
}

static void foundry_extension_object_call_script_method(FoundryExtensionObjectPtr p_object, FoundryExtensionConstStringNamePtr p_method, const FoundryExtensionConstVariantPtr *p_args, FoundryExtensionInt p_argument_count, FoundryExtensionUninitializedVariantPtr r_return, FoundryExtensionCallError *r_error) {
	Object *o = (Object *)p_object;
	const StringName method = *reinterpret_cast<const StringName *>(p_method);
	const Variant **args = (const Variant **)p_args;

	Callable::CallError error; // TODO: Check `error`?
	memnew_placement(r_return, Variant);
	*(Variant *)r_return = o->callp(method, args, p_argument_count, error);

	if (r_error) {
		r_error->error = (FoundryExtensionCallErrorType)(error.error);
		r_error->argument = error.argument;
		r_error->expected = error.expected;
	}
}

static FoundryExtensionObjectPtr foundry_extension_ref_get_object(FoundryExtensionConstRefPtr p_ref) {
	const Ref<RefCounted> *ref = (const Ref<RefCounted> *)p_ref;
	if (ref == nullptr || ref->is_null()) {
		return (FoundryExtensionObjectPtr) nullptr;
	} else {
		return (FoundryExtensionObjectPtr)ref->ptr();
	}
}

static void foundry_extension_ref_set_object(FoundryExtensionRefPtr p_ref, FoundryExtensionObjectPtr p_object) {
	Ref<RefCounted> *ref = (Ref<RefCounted> *)p_ref;
	ERR_FAIL_NULL(ref);

	Object *o = (RefCounted *)p_object;
	ref->reference_ptr(o);
}

#ifndef DISABLE_DEPRECATED
static FoundryExtensionScriptInstancePtr foundry_extension_script_instance_create(const FoundryExtensionScriptInstanceInfo *p_info, FoundryExtensionScriptInstanceDataPtr p_instance_data) {
	FoundryExtensionScriptInstanceInfo3 *info_3 = memnew(FoundryExtensionScriptInstanceInfo3);
	info_3->set_func = p_info->set_func;
	info_3->get_func = p_info->get_func;
	info_3->get_property_list_func = p_info->get_property_list_func;
	info_3->free_property_list_func = nullptr;
	info_3->get_class_category_func = nullptr;
	info_3->property_can_revert_func = p_info->property_can_revert_func;
	info_3->property_get_revert_func = p_info->property_get_revert_func;
	info_3->get_owner_func = p_info->get_owner_func;
	info_3->get_property_state_func = p_info->get_property_state_func;
	info_3->get_method_list_func = p_info->get_method_list_func;
	info_3->free_method_list_func = nullptr;
	info_3->get_property_type_func = p_info->get_property_type_func;
	info_3->validate_property_func = nullptr;
	info_3->has_method_func = p_info->has_method_func;
	info_3->get_method_argument_count_func = nullptr;
	info_3->call_func = p_info->call_func;
	info_3->notification_func = nullptr;
	info_3->to_string_func = p_info->to_string_func;
	info_3->refcount_incremented_func = p_info->refcount_incremented_func;
	info_3->refcount_decremented_func = p_info->refcount_decremented_func;
	info_3->get_script_func = p_info->get_script_func;
	info_3->is_placeholder_func = p_info->is_placeholder_func;
	info_3->set_fallback_func = p_info->set_fallback_func;
	info_3->get_fallback_func = p_info->get_fallback_func;
	info_3->get_language_func = p_info->get_language_func;
	info_3->free_func = p_info->free_func;

	ScriptInstanceExtension *script_instance_extension = memnew(ScriptInstanceExtension);
	script_instance_extension->instance = p_instance_data;
	script_instance_extension->native_info = info_3;
	script_instance_extension->free_native_info = true;
	script_instance_extension->deprecated_native_info = memnew(ScriptInstanceExtension::DeprecatedNativeInfo);
	script_instance_extension->deprecated_native_info->notification_func = p_info->notification_func;
	script_instance_extension->deprecated_native_info->free_property_list_func = p_info->free_property_list_func;
	script_instance_extension->deprecated_native_info->free_method_list_func = p_info->free_method_list_func;
	return reinterpret_cast<FoundryExtensionScriptInstancePtr>(script_instance_extension);
}

static FoundryExtensionScriptInstancePtr foundry_extension_script_instance_create2(const FoundryExtensionScriptInstanceInfo2 *p_info, FoundryExtensionScriptInstanceDataPtr p_instance_data) {
	FoundryExtensionScriptInstanceInfo3 *info_3 = memnew(FoundryExtensionScriptInstanceInfo3);
	info_3->set_func = p_info->set_func;
	info_3->get_func = p_info->get_func;
	info_3->get_property_list_func = p_info->get_property_list_func;
	info_3->free_property_list_func = nullptr;
	info_3->get_class_category_func = nullptr;
	info_3->property_can_revert_func = p_info->property_can_revert_func;
	info_3->property_get_revert_func = p_info->property_get_revert_func;
	info_3->get_owner_func = p_info->get_owner_func;
	info_3->get_property_state_func = p_info->get_property_state_func;
	info_3->get_method_list_func = p_info->get_method_list_func;
	info_3->free_method_list_func = nullptr;
	info_3->get_property_type_func = p_info->get_property_type_func;
	info_3->validate_property_func = nullptr;
	info_3->has_method_func = p_info->has_method_func;
	info_3->get_method_argument_count_func = nullptr;
	info_3->call_func = p_info->call_func;
	info_3->notification_func = p_info->notification_func;
	info_3->to_string_func = p_info->to_string_func;
	info_3->refcount_incremented_func = p_info->refcount_incremented_func;
	info_3->refcount_decremented_func = p_info->refcount_decremented_func;
	info_3->get_script_func = p_info->get_script_func;
	info_3->is_placeholder_func = p_info->is_placeholder_func;
	info_3->set_fallback_func = p_info->set_fallback_func;
	info_3->get_fallback_func = p_info->get_fallback_func;
	info_3->get_language_func = p_info->get_language_func;
	info_3->free_func = p_info->free_func;

	ScriptInstanceExtension *script_instance_extension = memnew(ScriptInstanceExtension);
	script_instance_extension->instance = p_instance_data;
	script_instance_extension->native_info = info_3;
	script_instance_extension->free_native_info = true;
	script_instance_extension->deprecated_native_info = memnew(ScriptInstanceExtension::DeprecatedNativeInfo);
	script_instance_extension->deprecated_native_info->free_property_list_func = p_info->free_property_list_func;
	script_instance_extension->deprecated_native_info->free_method_list_func = p_info->free_method_list_func;
	return reinterpret_cast<FoundryExtensionScriptInstancePtr>(script_instance_extension);
}
#endif // DISABLE_DEPRECATED

static FoundryExtensionScriptInstancePtr foundry_extension_script_instance_create3(const FoundryExtensionScriptInstanceInfo3 *p_info, FoundryExtensionScriptInstanceDataPtr p_instance_data) {
	ScriptInstanceExtension *script_instance_extension = memnew(ScriptInstanceExtension);
	script_instance_extension->instance = p_instance_data;
	script_instance_extension->native_info = p_info;
	return reinterpret_cast<FoundryExtensionScriptInstancePtr>(script_instance_extension);
}

static FoundryExtensionScriptInstancePtr foundry_extension_placeholder_script_instance_create(FoundryExtensionObjectPtr p_language, FoundryExtensionObjectPtr p_script, FoundryExtensionObjectPtr p_owner) {
	ScriptLanguage *language = (ScriptLanguage *)p_language;
	Ref<Script> script;
	script.reference_ptr((Script *)p_script);
	Object *owner = (Object *)p_owner;

	PlaceHolderScriptInstance *placeholder = memnew(PlaceHolderScriptInstance(language, script, owner));
	return reinterpret_cast<FoundryExtensionScriptInstancePtr>(placeholder);
}

static void foundry_extension_placeholder_script_instance_update(FoundryExtensionScriptInstancePtr p_placeholder, FoundryExtensionConstTypePtr p_properties, FoundryExtensionConstTypePtr p_values) {
	PlaceHolderScriptInstance *placeholder = dynamic_cast<PlaceHolderScriptInstance *>(reinterpret_cast<ScriptInstance *>(p_placeholder));
	ERR_FAIL_NULL_MSG(placeholder, "Unable to update placeholder, expected a PlaceHolderScriptInstance but received an invalid type.");

	const Array &properties = *reinterpret_cast<const Array *>(p_properties);
	const Dictionary &values = *reinterpret_cast<const Dictionary *>(p_values);

	List<PropertyInfo> properties_list;
	HashMap<StringName, Variant> values_map;

	for (int i = 0; i < properties.size(); i++) {
		Dictionary d = properties[i];
		properties_list.push_back(PropertyInfo::from_dict(d));
	}

	for (const KeyValue<Variant, Variant> &kv : values) {
		values_map.insert(kv.key, kv.value);
	}

	placeholder->update(properties_list, values_map);
}

static FoundryExtensionScriptInstancePtr foundry_extension_object_get_script_instance(FoundryExtensionConstObjectPtr p_object, FoundryExtensionConstObjectPtr p_language) {
	if (!p_object || !p_language) {
		return nullptr;
	}

	const Object *o = (const Object *)p_object;
	ScriptInstanceExtension *script_instance_extension = reinterpret_cast<ScriptInstanceExtension *>(o->get_script_instance());
	if (!script_instance_extension) {
		return nullptr;
	}

	const ScriptLanguage *language = script_instance_extension->get_language();
	if (language != p_language) {
		return nullptr;
	}

	return script_instance_extension->instance;
}

static void foundry_extension_object_set_script_instance(FoundryExtensionObjectPtr p_object, FoundryExtensionScriptInstancePtr p_script_instance) {
	ERR_FAIL_NULL(p_object);

	Object *o = (Object *)p_object;
	ScriptInstance *script_instance = (ScriptInstanceExtension *)p_script_instance;

	o->set_script_instance(script_instance);
}

#ifndef DISABLE_DEPRECATED
static void foundry_extension_callable_custom_create(FoundryExtensionUninitializedTypePtr r_callable, FoundryExtensionCallableCustomInfo *p_custom_callable_info) {
	memnew_placement(r_callable, Callable(memnew(CallableCustomExtension(p_custom_callable_info))));
}
#endif

static void foundry_extension_callable_custom_create2(FoundryExtensionUninitializedTypePtr r_callable, FoundryExtensionCallableCustomInfo2 *p_custom_callable_info) {
	memnew_placement(r_callable, Callable(memnew(CallableCustomExtension(p_custom_callable_info))));
}

static void *foundry_extension_callable_custom_get_userdata(FoundryExtensionTypePtr p_callable, void *p_token) {
	const Callable &callable = *reinterpret_cast<const Callable *>(p_callable);
	if (!callable.is_custom()) {
		return nullptr;
	}
	const CallableCustomExtension *custom_callable = dynamic_cast<const CallableCustomExtension *>(callable.get_custom());
	if (!custom_callable) {
		return nullptr;
	}
	return custom_callable->get_userdata(p_token);
}

static FoundryExtensionMethodBindPtr foundry_extension_classdb_get_method_bind(FoundryExtensionConstStringNamePtr p_classname, FoundryExtensionConstStringNamePtr p_methodname, FoundryExtensionInt p_hash) {
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	const StringName methodname = *reinterpret_cast<const StringName *>(p_methodname);
	bool exists = false;
	MethodBind *mb = ClassDB::get_method_with_compatibility(classname, methodname, p_hash, &exists);

#ifndef DISABLE_DEPRECATED
	// If lookup failed, see if this is one of the broken hashes from issue #81386.
	if (!mb && exists) {
		uint32_t mapped_hash;
		if (FoundryExtensionSpecialCompatHashes::lookup_current_hash(classname, methodname, p_hash, &mapped_hash)) {
			mb = ClassDB::get_method_with_compatibility(classname, methodname, mapped_hash, &exists);
		}
	}
#endif

	if (!mb && exists) {
		ERR_PRINT(vformat("Method '%s.%s' has changed and no compatibility fallback has been provided. Please open an issue.", classname, methodname));
		return nullptr;
	}
	ERR_FAIL_NULL_V(mb, nullptr);
	return (FoundryExtensionMethodBindPtr)mb;
}

#ifndef DISABLE_DEPRECATED
static FoundryExtensionObjectPtr foundry_extension_classdb_construct_object(FoundryExtensionConstStringNamePtr p_classname) {
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	return (FoundryExtensionObjectPtr)ClassDB::instantiate_no_placeholders(classname);
}
#endif

static FoundryExtensionObjectPtr foundry_extension_classdb_construct_object2(FoundryExtensionConstStringNamePtr p_classname) {
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	return (FoundryExtensionObjectPtr)ClassDB::instantiate_without_postinitialization(classname);
}

static void *foundry_extension_classdb_get_class_tag(FoundryExtensionConstStringNamePtr p_classname) {
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	ClassDB::ClassInfo *class_info = ClassDB::classes.getptr(classname);
	return class_info ? class_info->class_ptr : nullptr;
}

static void foundry_extension_editor_add_plugin(FoundryExtensionConstStringNamePtr p_classname) {
#ifdef TOOLS_ENABLED
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	FoundryExtensionEditorPlugins::add_extension_class(classname);
#endif
}

static void foundry_extension_editor_remove_plugin(FoundryExtensionConstStringNamePtr p_classname) {
#ifdef TOOLS_ENABLED
	const StringName classname = *reinterpret_cast<const StringName *>(p_classname);
	FoundryExtensionEditorPlugins::remove_extension_class(classname);
#endif
}

static void foundry_extension_editor_help_load_xml_from_utf8_chars_and_len(const char *p_data, FoundryExtensionInt p_size) {
#ifdef TOOLS_ENABLED
	FoundryExtensionEditorHelp::load_xml_buffer((const uint8_t *)p_data, p_size);
#endif
}

static void foundry_extension_editor_help_load_xml_from_utf8_chars(const char *p_data) {
#ifdef TOOLS_ENABLED
	size_t len = strlen(p_data);
	foundry_extension_editor_help_load_xml_from_utf8_chars_and_len(p_data, len);
#endif
}

#define REGISTER_INTERFACE_FUNC(m_name) FoundryExtension::register_interface_function(#m_name, (FoundryExtensionInterfaceFunctionPtr) & foundry_extension_##m_name)

void foundry_extension_setup_interface() {
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(get_foundry_version);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(get_foundry_version2);
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(mem_alloc);
	REGISTER_INTERFACE_FUNC(mem_realloc);
	REGISTER_INTERFACE_FUNC(mem_free);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(mem_alloc2);
	REGISTER_INTERFACE_FUNC(mem_realloc2);
	REGISTER_INTERFACE_FUNC(mem_free2);
	REGISTER_INTERFACE_FUNC(print_error);
	REGISTER_INTERFACE_FUNC(print_error_with_message);
	REGISTER_INTERFACE_FUNC(print_warning);
	REGISTER_INTERFACE_FUNC(print_warning_with_message);
	REGISTER_INTERFACE_FUNC(print_script_error);
	REGISTER_INTERFACE_FUNC(print_script_error_with_message);
	REGISTER_INTERFACE_FUNC(get_native_struct_size);
	REGISTER_INTERFACE_FUNC(variant_new_copy);
	REGISTER_INTERFACE_FUNC(variant_new_nil);
	REGISTER_INTERFACE_FUNC(variant_destroy);
	REGISTER_INTERFACE_FUNC(variant_call);
	REGISTER_INTERFACE_FUNC(variant_call_static);
	REGISTER_INTERFACE_FUNC(variant_evaluate);
	REGISTER_INTERFACE_FUNC(variant_set);
	REGISTER_INTERFACE_FUNC(variant_set_named);
	REGISTER_INTERFACE_FUNC(variant_set_keyed);
	REGISTER_INTERFACE_FUNC(variant_set_indexed);
	REGISTER_INTERFACE_FUNC(variant_get);
	REGISTER_INTERFACE_FUNC(variant_get_named);
	REGISTER_INTERFACE_FUNC(variant_get_keyed);
	REGISTER_INTERFACE_FUNC(variant_get_indexed);
	REGISTER_INTERFACE_FUNC(variant_iter_init);
	REGISTER_INTERFACE_FUNC(variant_iter_next);
	REGISTER_INTERFACE_FUNC(variant_iter_get);
	REGISTER_INTERFACE_FUNC(variant_hash);
	REGISTER_INTERFACE_FUNC(variant_recursive_hash);
	REGISTER_INTERFACE_FUNC(variant_hash_compare);
	REGISTER_INTERFACE_FUNC(variant_booleanize);
	REGISTER_INTERFACE_FUNC(variant_duplicate);
	REGISTER_INTERFACE_FUNC(variant_stringify);
	REGISTER_INTERFACE_FUNC(variant_get_type);
	REGISTER_INTERFACE_FUNC(variant_has_method);
	REGISTER_INTERFACE_FUNC(variant_has_member);
	REGISTER_INTERFACE_FUNC(variant_has_key);
	REGISTER_INTERFACE_FUNC(variant_get_object_instance_id);
	REGISTER_INTERFACE_FUNC(variant_get_type_name);
	REGISTER_INTERFACE_FUNC(variant_can_convert);
	REGISTER_INTERFACE_FUNC(variant_can_convert_strict);
	REGISTER_INTERFACE_FUNC(get_variant_from_type_constructor);
	REGISTER_INTERFACE_FUNC(get_variant_to_type_constructor);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_internal_getter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_operator_evaluator);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_builtin_method);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_constructor);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_destructor);
	REGISTER_INTERFACE_FUNC(variant_construct);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_setter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_getter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_indexed_setter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_indexed_getter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_keyed_setter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_keyed_getter);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_keyed_checker);
	REGISTER_INTERFACE_FUNC(variant_get_constant_value);
	REGISTER_INTERFACE_FUNC(variant_get_ptr_utility_function);
	REGISTER_INTERFACE_FUNC(string_new_with_latin1_chars);
	REGISTER_INTERFACE_FUNC(string_new_with_utf8_chars);
	REGISTER_INTERFACE_FUNC(string_new_with_utf16_chars);
	REGISTER_INTERFACE_FUNC(string_new_with_utf32_chars);
	REGISTER_INTERFACE_FUNC(string_new_with_wide_chars);
	REGISTER_INTERFACE_FUNC(string_new_with_latin1_chars_and_len);
	REGISTER_INTERFACE_FUNC(string_new_with_utf8_chars_and_len);
	REGISTER_INTERFACE_FUNC(string_new_with_utf8_chars_and_len2);
	REGISTER_INTERFACE_FUNC(string_new_with_utf16_chars_and_len);
	REGISTER_INTERFACE_FUNC(string_new_with_utf16_chars_and_len2);
	REGISTER_INTERFACE_FUNC(string_new_with_utf32_chars_and_len);
	REGISTER_INTERFACE_FUNC(string_new_with_wide_chars_and_len);
	REGISTER_INTERFACE_FUNC(string_to_latin1_chars);
	REGISTER_INTERFACE_FUNC(string_to_utf8_chars);
	REGISTER_INTERFACE_FUNC(string_to_utf16_chars);
	REGISTER_INTERFACE_FUNC(string_to_utf32_chars);
	REGISTER_INTERFACE_FUNC(string_to_wide_chars);
	REGISTER_INTERFACE_FUNC(string_operator_index);
	REGISTER_INTERFACE_FUNC(string_operator_index_const);
	REGISTER_INTERFACE_FUNC(string_operator_plus_eq_string);
	REGISTER_INTERFACE_FUNC(string_operator_plus_eq_char);
	REGISTER_INTERFACE_FUNC(string_operator_plus_eq_cstr);
	REGISTER_INTERFACE_FUNC(string_operator_plus_eq_wcstr);
	REGISTER_INTERFACE_FUNC(string_operator_plus_eq_c32str);
	REGISTER_INTERFACE_FUNC(string_resize);
	REGISTER_INTERFACE_FUNC(string_name_new_with_latin1_chars);
	REGISTER_INTERFACE_FUNC(string_name_new_with_utf8_chars);
	REGISTER_INTERFACE_FUNC(string_name_new_with_utf8_chars_and_len);
	REGISTER_INTERFACE_FUNC(xml_parser_open_buffer);
	REGISTER_INTERFACE_FUNC(file_access_store_buffer);
	REGISTER_INTERFACE_FUNC(file_access_get_buffer);
	REGISTER_INTERFACE_FUNC(worker_thread_pool_add_native_group_task);
	REGISTER_INTERFACE_FUNC(worker_thread_pool_add_native_task);
	REGISTER_INTERFACE_FUNC(packed_byte_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_byte_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_color_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_color_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_float32_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_float32_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_float64_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_float64_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_int32_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_int32_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_int64_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_int64_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_string_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_string_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_vector2_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_vector2_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_vector3_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_vector3_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(packed_vector4_array_operator_index);
	REGISTER_INTERFACE_FUNC(packed_vector4_array_operator_index_const);
	REGISTER_INTERFACE_FUNC(array_operator_index);
	REGISTER_INTERFACE_FUNC(array_operator_index_const);
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(array_ref);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(array_set_typed);
	REGISTER_INTERFACE_FUNC(array_set_typed_by_descriptor);
	REGISTER_INTERFACE_FUNC(array_get_typed_element_type_descriptor);
	REGISTER_INTERFACE_FUNC(dictionary_operator_index);
	REGISTER_INTERFACE_FUNC(dictionary_operator_index_const);
	REGISTER_INTERFACE_FUNC(dictionary_set_typed);
	REGISTER_INTERFACE_FUNC(dictionary_set_typed_by_descriptor);
	REGISTER_INTERFACE_FUNC(dictionary_get_typed_key_type_descriptor);
	REGISTER_INTERFACE_FUNC(dictionary_get_typed_value_type_descriptor);
	REGISTER_INTERFACE_FUNC(object_method_bind_call);
	REGISTER_INTERFACE_FUNC(object_method_bind_ptrcall);
	REGISTER_INTERFACE_FUNC(object_destroy);
	REGISTER_INTERFACE_FUNC(global_get_singleton);
	REGISTER_INTERFACE_FUNC(object_get_instance_binding);
	REGISTER_INTERFACE_FUNC(object_set_instance_binding);
	REGISTER_INTERFACE_FUNC(object_free_instance_binding);
	REGISTER_INTERFACE_FUNC(object_set_instance);
	REGISTER_INTERFACE_FUNC(object_get_class_name);
	REGISTER_INTERFACE_FUNC(object_cast_to);
	REGISTER_INTERFACE_FUNC(object_get_instance_from_id);
	REGISTER_INTERFACE_FUNC(object_get_instance_id);
	REGISTER_INTERFACE_FUNC(object_has_script_method);
	REGISTER_INTERFACE_FUNC(object_call_script_method);
	REGISTER_INTERFACE_FUNC(ref_get_object);
	REGISTER_INTERFACE_FUNC(ref_set_object);
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(script_instance_create);
	REGISTER_INTERFACE_FUNC(script_instance_create2);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(script_instance_create3);
	REGISTER_INTERFACE_FUNC(placeholder_script_instance_create);
	REGISTER_INTERFACE_FUNC(placeholder_script_instance_update);
	REGISTER_INTERFACE_FUNC(object_get_script_instance);
	REGISTER_INTERFACE_FUNC(object_set_script_instance);
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(callable_custom_create);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(callable_custom_create2);
	REGISTER_INTERFACE_FUNC(callable_custom_get_userdata);
#ifndef DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(classdb_construct_object);
#endif // DISABLE_DEPRECATED
	REGISTER_INTERFACE_FUNC(classdb_construct_object2);
	REGISTER_INTERFACE_FUNC(classdb_get_method_bind);
	REGISTER_INTERFACE_FUNC(classdb_get_class_tag);
	REGISTER_INTERFACE_FUNC(editor_add_plugin);
	REGISTER_INTERFACE_FUNC(editor_remove_plugin);
	REGISTER_INTERFACE_FUNC(editor_help_load_xml_from_utf8_chars);
	REGISTER_INTERFACE_FUNC(editor_help_load_xml_from_utf8_chars_and_len);
	REGISTER_INTERFACE_FUNC(image_ptrw);
	REGISTER_INTERFACE_FUNC(image_ptr);
}

#undef REGISTER_INTERFACE_FUNCTION
