/**************************************************************************/
/*  native_ptr.h                                                          */
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

#pragma once

#include "core/math/audio_frame.h"
#include "core/variant/binder_common.h"
#include "core/variant/method_ptrcall.h"
#include "core/variant/type_info.h"

template <typename T>
struct FoundryExtensionConstPtr {
	const T *data = nullptr;
	FoundryExtensionConstPtr(const T *p_assign) { data = p_assign; }
	static const char *get_name() { return "const void"; }
	operator const T *() const { return data; }
	operator Variant() const { return uint64_t(data); }
};

template <typename T>
struct FoundryExtensionPtr {
	T *data = nullptr;
	FoundryExtensionPtr(T *p_assign) { data = p_assign; }
	static const char *get_name() { return "void"; }
	operator T *() const { return data; }
	operator Variant() const { return uint64_t(data); }
};

#define FOUNDRY_VIRTUAL_NATIVE_PTR(m_type)                                                                         \
	template <>                                                                                                    \
	struct FoundryExtensionConstPtr<const m_type> {                                                                \
		const m_type *data = nullptr;                                                                              \
		FoundryExtensionConstPtr() {}                                                                              \
		FoundryExtensionConstPtr(const m_type *p_assign) {                                                         \
			data = p_assign;                                                                                       \
		}                                                                                                          \
		static const char *get_name() {                                                                            \
			return "const " #m_type;                                                                               \
		}                                                                                                          \
		operator const m_type *() const {                                                                          \
			return data;                                                                                           \
		}                                                                                                          \
		operator Variant() const {                                                                                 \
			return uint64_t(data);                                                                                 \
		}                                                                                                          \
	};                                                                                                             \
	template <>                                                                                                    \
	struct VariantCaster<FoundryExtensionConstPtr<const m_type>> {                                                 \
		static _FORCE_INLINE_ FoundryExtensionConstPtr<const m_type> cast(const Variant &p_variant) {              \
			return FoundryExtensionConstPtr<const m_type>((const m_type *)p_variant.operator uint64_t());          \
		}                                                                                                          \
	};                                                                                                             \
	template <>                                                                                                    \
	struct VariantInternalAccessor<FoundryExtensionConstPtr<const m_type>> {                                       \
		static _FORCE_INLINE_ const FoundryExtensionConstPtr<const m_type> &get(const Variant *v) {                \
			return *reinterpret_cast<const FoundryExtensionConstPtr<const m_type> *>(VariantInternal::get_int(v)); \
		}                                                                                                          \
		static _FORCE_INLINE_ void set(Variant *v, const FoundryExtensionConstPtr<const m_type> &p_value) {        \
			*VariantInternal::get_int(v) = uint64_t(p_value.data);                                                 \
		}                                                                                                          \
	};                                                                                                             \
	template <>                                                                                                    \
	struct FoundryExtensionPtr<m_type> {                                                                           \
		m_type *data = nullptr;                                                                                    \
		FoundryExtensionPtr() {}                                                                                   \
		FoundryExtensionPtr(m_type *p_assign) {                                                                    \
			data = p_assign;                                                                                       \
		}                                                                                                          \
		static const char *get_name() {                                                                            \
			return #m_type;                                                                                        \
		}                                                                                                          \
		operator m_type *() const {                                                                                \
			return data;                                                                                           \
		}                                                                                                          \
		operator Variant() const {                                                                                 \
			return uint64_t(data);                                                                                 \
		}                                                                                                          \
	};                                                                                                             \
	template <>                                                                                                    \
	struct VariantCaster<FoundryExtensionPtr<m_type>> {                                                            \
		static _FORCE_INLINE_ FoundryExtensionPtr<m_type> cast(const Variant &p_variant) {                         \
			return FoundryExtensionPtr<m_type>((m_type *)p_variant.operator uint64_t());                           \
		}                                                                                                          \
	};                                                                                                             \
	template <>                                                                                                    \
	struct VariantInternalAccessor<FoundryExtensionPtr<m_type>> {                                                  \
		static _FORCE_INLINE_ const FoundryExtensionPtr<m_type> &get(const Variant *v) {                           \
			return *reinterpret_cast<const FoundryExtensionPtr<m_type> *>(VariantInternal::get_int(v));            \
		}                                                                                                          \
		static _FORCE_INLINE_ void set(Variant *v, const FoundryExtensionPtr<m_type> &p_value) {                   \
			*VariantInternal::get_int(v) = uint64_t(p_value.data);                                                 \
		}                                                                                                          \
	};

template <typename T>
struct GetTypeInfo<FoundryExtensionConstPtr<T>> {
	static const Variant::Type VARIANT_TYPE = Variant::INT;
	static const FoundryTypeInfo::Metadata METADATA = FoundryTypeInfo::METADATA_NONE;
	static inline PropertyInfo get_class_info() {
		return PropertyInfo(Variant::INT, String(), PROPERTY_HINT_INT_IS_POINTER, FoundryExtensionConstPtr<T>::get_name());
	}
};

template <typename T>
struct GetTypeInfo<FoundryExtensionPtr<T>> {
	static const Variant::Type VARIANT_TYPE = Variant::INT;
	static const FoundryTypeInfo::Metadata METADATA = FoundryTypeInfo::METADATA_NONE;
	static inline PropertyInfo get_class_info() {
		return PropertyInfo(Variant::INT, String(), PROPERTY_HINT_INT_IS_POINTER, FoundryExtensionPtr<T>::get_name());
	}
};

template <typename T>
struct PtrToArg<FoundryExtensionConstPtr<T>> {
	_FORCE_INLINE_ static FoundryExtensionConstPtr<T> convert(const void *p_ptr) {
		return FoundryExtensionConstPtr<T>(reinterpret_cast<const T *>(p_ptr));
	}
	typedef const T *EncodeT;
	_FORCE_INLINE_ static void encode(FoundryExtensionConstPtr<T> p_val, void *p_ptr) {
		*((const T **)p_ptr) = p_val.data;
	}
};
template <typename T>
struct PtrToArg<FoundryExtensionPtr<T>> {
	_FORCE_INLINE_ static FoundryExtensionPtr<T> convert(const void *p_ptr) {
		return FoundryExtensionPtr<T>(reinterpret_cast<const T *>(p_ptr));
	}
	typedef T *EncodeT;
	_FORCE_INLINE_ static void encode(FoundryExtensionPtr<T> p_val, void *p_ptr) {
		*((T **)p_ptr) = p_val.data;
	}
};

FOUNDRY_VIRTUAL_NATIVE_PTR(void)
FOUNDRY_VIRTUAL_NATIVE_PTR(AudioFrame)
FOUNDRY_VIRTUAL_NATIVE_PTR(bool)
FOUNDRY_VIRTUAL_NATIVE_PTR(char)
FOUNDRY_VIRTUAL_NATIVE_PTR(char16_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(char32_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(wchar_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(uint8_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(uint8_t *)
FOUNDRY_VIRTUAL_NATIVE_PTR(int8_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(uint16_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(int16_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(uint32_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(int32_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(int64_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(uint64_t)
FOUNDRY_VIRTUAL_NATIVE_PTR(float)
FOUNDRY_VIRTUAL_NATIVE_PTR(double)
