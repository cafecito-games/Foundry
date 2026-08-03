/**************************************************************************/
/*  variant_op.h                                                          */
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

#include "variant.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/class_db.h"
#include "core/variant/container_type_validate.h"

#include <limits>
#include <type_traits>

template <typename Evaluator>
class CommonEvaluate {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		VariantTypeChanger<typename Evaluator::ReturnType>::change(r_ret);
		Evaluator::validated_evaluate(&p_left, &p_right, r_ret);
		r_valid = true;
	}
	static Variant::Type get_return_type() { return GetTypeInfo<typename Evaluator::ReturnType>::VARIANT_TYPE; }
};

template <typename R, typename A, typename B>
class OperatorEvaluatorAdd : public CommonEvaluate<OperatorEvaluatorAdd<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) + VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) + PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorSub : public CommonEvaluate<OperatorEvaluatorSub<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) - VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) - PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorMul : public CommonEvaluate<OperatorEvaluatorMul<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) * VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) * PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorPow : public CommonEvaluate<OperatorEvaluatorPow<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = R(Math::pow((double)VariantInternalAccessor<A>::get(left), (double)VariantInternalAccessor<B>::get(right)));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(R(Math::pow((double)PtrToArg<A>::convert(left), (double)PtrToArg<B>::convert(right))), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorXForm : public CommonEvaluate<OperatorEvaluatorXForm<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left).xform(VariantInternalAccessor<B>::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left).xform(PtrToArg<B>::convert(right)), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorXFormInv : public CommonEvaluate<OperatorEvaluatorXFormInv<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<B>::get(right).xform_inv(VariantInternalAccessor<A>::get(left));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<B>::convert(right).xform_inv(PtrToArg<A>::convert(left)), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorDiv : public CommonEvaluate<OperatorEvaluatorDiv<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) / VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) / PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorDivNZ {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const A &a = VariantInternalAccessor<A>::get(&p_left);
		const B &b = VariantInternalAccessor<B>::get(&p_right);
		if (b == 0) {
			r_valid = false;
			*r_ret = "Division by zero error";
			return;
		}
		*r_ret = a / b;
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) / VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) / PtrToArg<B>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<R>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorDivNZ<Vector2i, Vector2i, Vector2i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector2i &a = VariantInternalAccessor<Vector2i>::get(&p_left);
		const Vector2i &b = VariantInternalAccessor<Vector2i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0)) {
			r_valid = false;
			*r_ret = "Division by zero error";
			return;
		}
		*r_ret = a / b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector2i>::change(r_ret);
		VariantInternalAccessor<Vector2i>::get(r_ret) = VariantInternalAccessor<Vector2i>::get(left) / VariantInternalAccessor<Vector2i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector2i>::encode(PtrToArg<Vector2i>::convert(left) / PtrToArg<Vector2i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector2i>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorDivNZ<Vector3i, Vector3i, Vector3i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector3i &a = VariantInternalAccessor<Vector3i>::get(&p_left);
		const Vector3i &b = VariantInternalAccessor<Vector3i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0 || b.z == 0)) {
			r_valid = false;
			*r_ret = "Division by zero error";
			return;
		}
		*r_ret = a / b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector3i>::change(r_ret);
		VariantInternalAccessor<Vector3i>::get(r_ret) = VariantInternalAccessor<Vector3i>::get(left) / VariantInternalAccessor<Vector3i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector3i>::encode(PtrToArg<Vector3i>::convert(left) / PtrToArg<Vector3i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector3i>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorDivNZ<Vector4i, Vector4i, Vector4i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector4i &a = VariantInternalAccessor<Vector4i>::get(&p_left);
		const Vector4i &b = VariantInternalAccessor<Vector4i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0 || b.z == 0 || b.w == 0)) {
			r_valid = false;
			*r_ret = "Division by zero error";
			return;
		}
		*r_ret = a / b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector4i>::change(r_ret);
		VariantInternalAccessor<Vector4i>::get(r_ret) = VariantInternalAccessor<Vector4i>::get(left) / VariantInternalAccessor<Vector4i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector4i>::encode(PtrToArg<Vector4i>::convert(left) / PtrToArg<Vector4i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector4i>::VARIANT_TYPE; }
};

template <typename R, typename A, typename B>
class OperatorEvaluatorMod : public CommonEvaluate<OperatorEvaluatorMod<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) % VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) % PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorModNZ {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const A &a = VariantInternalAccessor<A>::get(&p_left);
		const B &b = VariantInternalAccessor<B>::get(&p_right);
		if (b == 0) {
			r_valid = false;
			*r_ret = "Modulo by zero error";
			return;
		}
		*r_ret = a % b;
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) % VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) % PtrToArg<B>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<R>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorModNZ<Vector2i, Vector2i, Vector2i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector2i &a = VariantInternalAccessor<Vector2i>::get(&p_left);
		const Vector2i &b = VariantInternalAccessor<Vector2i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0)) {
			r_valid = false;
			*r_ret = "Modulo by zero error";
			return;
		}
		*r_ret = a % b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector2i>::change(r_ret);
		VariantInternalAccessor<Vector2i>::get(r_ret) = VariantInternalAccessor<Vector2i>::get(left) % VariantInternalAccessor<Vector2i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector2i>::encode(PtrToArg<Vector2i>::convert(left) % PtrToArg<Vector2i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector2i>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorModNZ<Vector3i, Vector3i, Vector3i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector3i &a = VariantInternalAccessor<Vector3i>::get(&p_left);
		const Vector3i &b = VariantInternalAccessor<Vector3i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0 || b.z == 0)) {
			r_valid = false;
			*r_ret = "Modulo by zero error";
			return;
		}
		*r_ret = a % b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector3i>::change(r_ret);
		VariantInternalAccessor<Vector3i>::get(r_ret) = VariantInternalAccessor<Vector3i>::get(left) % VariantInternalAccessor<Vector3i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector3i>::encode(PtrToArg<Vector3i>::convert(left) % PtrToArg<Vector3i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector3i>::VARIANT_TYPE; }
};

template <>
class OperatorEvaluatorModNZ<Vector4i, Vector4i, Vector4i> {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const Vector4i &a = VariantInternalAccessor<Vector4i>::get(&p_left);
		const Vector4i &b = VariantInternalAccessor<Vector4i>::get(&p_right);
		if (unlikely(b.x == 0 || b.y == 0 || b.z == 0 || b.w == 0)) {
			r_valid = false;
			*r_ret = "Modulo by zero error";
			return;
		}
		*r_ret = a % b;
		r_valid = true;
	}
	static void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantTypeChanger<Vector4i>::change(r_ret);
		VariantInternalAccessor<Vector4i>::get(r_ret) = VariantInternalAccessor<Vector4i>::get(left) % VariantInternalAccessor<Vector4i>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<Vector4i>::encode(PtrToArg<Vector4i>::convert(left) % PtrToArg<Vector4i>::convert(right), r_ret);
	}
	static Variant::Type get_return_type() { return GetTypeInfo<Vector4i>::VARIANT_TYPE; }
};

template <typename R, typename A>
class OperatorEvaluatorNeg : public CommonEvaluate<OperatorEvaluatorNeg<R, A>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = -VariantInternalAccessor<A>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(-PtrToArg<A>::convert(left), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A>
class OperatorEvaluatorPos : public CommonEvaluate<OperatorEvaluatorPos<R, A>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorBitOr : public CommonEvaluate<OperatorEvaluatorBitOr<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) | VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) | PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorBitAnd : public CommonEvaluate<OperatorEvaluatorBitAnd<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) & VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) & PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A, typename B>
class OperatorEvaluatorBitXor : public CommonEvaluate<OperatorEvaluatorBitXor<R, A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = VariantInternalAccessor<A>::get(left) ^ VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(PtrToArg<A>::convert(left) ^ PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = R;
};

template <typename R, typename A>
class OperatorEvaluatorBitNeg : public CommonEvaluate<OperatorEvaluatorBitNeg<R, A>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<R>::get(r_ret) = ~VariantInternalAccessor<A>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<R>::encode(~PtrToArg<A>::convert(left), r_ret);
	}
	using ReturnType = R;
};

template <typename A, typename B>
class OperatorEvaluatorEqual : public CommonEvaluate<OperatorEvaluatorEqual<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) == VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) == PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

// The signed and unsigned integer carriers describe a single mathematical number line. Neither
// operand is ever cast into the other's carrier, so unsigned values above `INT64_MAX` keep their
// magnitude and negative signed values stay below every unsigned value.
struct VariantIntegerCompare {
	static _ALWAYS_INLINE_ bool equal(uint64_t p_left, uint64_t p_right) { return p_left == p_right; }
	static _ALWAYS_INLINE_ bool equal(int64_t p_left, uint64_t p_right) { return p_left >= 0 && uint64_t(p_left) == p_right; }
	static _ALWAYS_INLINE_ bool equal(uint64_t p_left, int64_t p_right) { return p_right >= 0 && p_left == uint64_t(p_right); }

	static _ALWAYS_INLINE_ bool less(uint64_t p_left, uint64_t p_right) { return p_left < p_right; }
	static _ALWAYS_INLINE_ bool less(int64_t p_left, uint64_t p_right) { return p_left < 0 || uint64_t(p_left) < p_right; }
	static _ALWAYS_INLINE_ bool less(uint64_t p_left, int64_t p_right) { return p_right >= 0 && p_left < uint64_t(p_right); }
};

struct VariantIntegerEqualOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return VariantIntegerCompare::equal(p_left, p_right); }
};

struct VariantIntegerNotEqualOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return !VariantIntegerCompare::equal(p_left, p_right); }
};

struct VariantIntegerLessOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return VariantIntegerCompare::less(p_left, p_right); }
};

struct VariantIntegerLessEqualOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return !VariantIntegerCompare::less(p_right, p_left); }
};

struct VariantIntegerGreaterOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return VariantIntegerCompare::less(p_right, p_left); }
};

struct VariantIntegerGreaterEqualOperation {
	template <typename Left, typename Right>
	static _ALWAYS_INLINE_ bool compare(Left p_left, Right p_right) { return !VariantIntegerCompare::less(p_left, p_right); }
};

// The unsigned carrier has no C++ nominal type of its own, so the integer comparison evaluators read
// each operand through an explicit carrier accessor instead of `VariantInternalAccessor`.
struct VariantIntCarrier {
	static _ALWAYS_INLINE_ int64_t get(const Variant *p_variant) { return *VariantInternal::get_int(p_variant); }
	static _ALWAYS_INLINE_ int64_t get_ptr(const void *p_ptr) { return PtrToArg<int64_t>::convert(p_ptr); }
};

struct VariantUIntCarrier {
	static _ALWAYS_INLINE_ uint64_t get(const Variant *p_variant) { return *VariantInternal::get_uint(p_variant); }
	static _ALWAYS_INLINE_ uint64_t get_ptr(const void *p_ptr) { return PtrToArg<uint64_t>::convert(p_ptr); }

	// `VariantTypeChanger` resolves its destination through `GetTypeInfo<T>`, which maps every C++
	// unsigned integer to the signed carrier, so unsigned results retag the destination here instead.
	static _ALWAYS_INLINE_ void change(Variant *r_ret) {
		if (r_ret->get_type() != Variant::UINT) {
			VariantInternal::clear(r_ret);
			VariantUIntInitializer::init(r_ret);
		}
	}
	static _ALWAYS_INLINE_ void set(Variant *r_ret, uint64_t p_value) { *VariantInternal::get_uint(r_ret) = p_value; }
	static _ALWAYS_INLINE_ Variant box(uint64_t p_value) {
		Variant value;
		VariantUIntInitializer::init(&value);
		*VariantInternal::get_uint(&value) = p_value;
		return value;
	}
};

// Unsigned arithmetic is defined modulo 2^64 by the C++ standard, so these operations wrap instead of
// invoking the signed overflow the equivalent `int64_t` evaluators would.
struct VariantUIntAddOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left + p_right; }
};

struct VariantUIntSubtractOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left - p_right; }
};

struct VariantUIntMultiplyOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left * p_right; }
};

struct VariantUIntBitOrOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left | p_right; }
};

struct VariantUIntBitAndOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left & p_right; }
};

struct VariantUIntBitXorOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left ^ p_right; }
};

// Exponentiation by squaring keeps every result below 2^64 exact. Routing the unsigned carrier through
// `Math::pow()` like the signed evaluator does would round results above the 53-bit double mantissa
// and would convert out-of-range doubles back to an integer, which is undefined.
struct VariantUIntPowerOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_base, uint64_t p_exponent) {
		uint64_t result = 1;
		uint64_t base = p_base;
		uint64_t exponent = p_exponent;
		while (exponent > 0) {
			if (exponent & 1) {
				result *= base;
			}
			exponent >>= 1;
			if (exponent > 0) {
				base *= base;
			}
		}
		return result;
	}
};

struct VariantUIntDivideOperation {
	static _ALWAYS_INLINE_ bool accepts(uint64_t p_right) { return p_right != 0; }
	static _ALWAYS_INLINE_ const char *error_message() { return "Division by zero error"; }
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left / p_right; }
};

struct VariantUIntModuloOperation {
	static _ALWAYS_INLINE_ bool accepts(uint64_t p_right) { return p_right != 0; }
	static _ALWAYS_INLINE_ const char *error_message() { return "Modulo by zero error"; }
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left % p_right; }
};

struct VariantUIntShiftLeftOperation {
	static _ALWAYS_INLINE_ bool accepts(uint64_t p_right) { return p_right < 64; }
	static _ALWAYS_INLINE_ const char *error_message() { return "Invalid operands for bit shifting. The shift count must be smaller than 64."; }
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left << p_right; }
};

struct VariantUIntShiftRightOperation {
	static _ALWAYS_INLINE_ bool accepts(uint64_t p_right) { return p_right < 64; }
	static _ALWAYS_INLINE_ const char *error_message() { return "Invalid operands for bit shifting. The shift count must be smaller than 64."; }
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_left, uint64_t p_right) { return p_left >> p_right; }
};

struct VariantUIntBitNegateOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_value) { return ~p_value; }
};

struct VariantUIntPositiveOperation {
	static _ALWAYS_INLINE_ uint64_t compute(uint64_t p_value) { return p_value; }
};

template <typename Operation>
class OperatorEvaluatorUIntBinary {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		// The result is computed before the destination is retagged so an aliased destination is safe.
		const uint64_t result = Operation::compute(VariantUIntCarrier::get(&p_left), VariantUIntCarrier::get(&p_right));
		VariantUIntCarrier::change(r_ret);
		VariantUIntCarrier::set(r_ret, result);
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantUIntCarrier::set(r_ret, Operation::compute(VariantUIntCarrier::get(left), VariantUIntCarrier::get(right)));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<uint64_t>::encode(Operation::compute(VariantUIntCarrier::get_ptr(left), VariantUIntCarrier::get_ptr(right)), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::UINT; }
};

// Division, remainder, and shifts have right operands the C++ operation cannot evaluate at all: a zero
// divisor, and a shift count that is not smaller than the operand width. Every entry point rejects
// those instead of running the undefined operation. `evaluate()` reports the failure through its
// validity flag; the validated and pointer entry points cannot, so they fall back to zero and raise an
// engine error, the same shape the string-format evaluators use for an unreportable failure.
template <typename Operation>
class OperatorEvaluatorUIntCheckedBinary {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const uint64_t left = VariantUIntCarrier::get(&p_left);
		const uint64_t right = VariantUIntCarrier::get(&p_right);
		if (unlikely(!Operation::accepts(right))) {
			*r_ret = Operation::error_message();
			r_valid = false;
			return;
		}
		const uint64_t result = Operation::compute(left, right);
		VariantUIntCarrier::change(r_ret);
		VariantUIntCarrier::set(r_ret, result);
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const uint64_t left_value = VariantUIntCarrier::get(left);
		const uint64_t right_value = VariantUIntCarrier::get(right);
		if (unlikely(!Operation::accepts(right_value))) {
			VariantUIntCarrier::set(r_ret, 0);
			ERR_FAIL_MSG(Operation::error_message());
		}
		VariantUIntCarrier::set(r_ret, Operation::compute(left_value, right_value));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		const uint64_t left_value = VariantUIntCarrier::get_ptr(left);
		const uint64_t right_value = VariantUIntCarrier::get_ptr(right);
		if (unlikely(!Operation::accepts(right_value))) {
			PtrToArg<uint64_t>::encode(0, r_ret);
			ERR_FAIL_MSG(Operation::error_message());
		}
		PtrToArg<uint64_t>::encode(Operation::compute(left_value, right_value), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::UINT; }
};

template <typename Operation>
class OperatorEvaluatorUIntUnary {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const uint64_t result = Operation::compute(VariantUIntCarrier::get(&p_left));
		VariantUIntCarrier::change(r_ret);
		VariantUIntCarrier::set(r_ret, result);
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantUIntCarrier::set(r_ret, Operation::compute(VariantUIntCarrier::get(left)));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<uint64_t>::encode(Operation::compute(VariantUIntCarrier::get_ptr(left)), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::UINT; }
};

// Reinterprets an unsigned bit pattern as the signed value with the same representation. A plain
// conversion is only guaranteed to do that from C++20 onward, so the mapping is spelled out.
static _ALWAYS_INLINE_ int64_t variant_int_from_bits(uint64_t p_bits) {
	if (p_bits <= uint64_t(INT64_MAX)) {
		return int64_t(p_bits);
	}
	return int64_t(p_bits - uint64_t(INT64_MAX) - 1) + INT64_MIN;
}

// Signed division, remainder, and shifts have operand pairs the C++ operation cannot evaluate at all:
// a zero divisor, a quotient that is not representable, a negative shift operand or count, and a
// count that is not smaller than the operand width. Acceptance depends on both operands, because the
// minimum dividend is only a problem against a divisor of -1.
struct VariantIntDivideOperation {
	static _ALWAYS_INLINE_ bool accepts(int64_t p_left, int64_t p_right) {
		return p_right != 0 && !(p_left == INT64_MIN && p_right == -1);
	}
	static _ALWAYS_INLINE_ const char *error_message(int64_t p_left, int64_t p_right) {
		if (p_right == 0) {
			return "Division by zero error";
		}
		return "Division overflow error. The quotient of the minimum integer and -1 is not representable.";
	}
	static _ALWAYS_INLINE_ int64_t compute(int64_t p_left, int64_t p_right) { return p_left / p_right; }
};

struct VariantIntModuloOperation {
	static _ALWAYS_INLINE_ bool accepts(int64_t p_left, int64_t p_right) {
		return p_right != 0 && !(p_left == INT64_MIN && p_right == -1);
	}
	static _ALWAYS_INLINE_ const char *error_message(int64_t p_left, int64_t p_right) {
		if (p_right == 0) {
			return "Modulo by zero error";
		}
		return "Modulo overflow error. The quotient of the minimum integer and -1 is not representable.";
	}
	static _ALWAYS_INLINE_ int64_t compute(int64_t p_left, int64_t p_right) { return p_left % p_right; }
};

struct VariantIntShiftLeftOperation {
	static _ALWAYS_INLINE_ bool accepts(int64_t p_left, int64_t p_right) {
		return p_left >= 0 && p_right >= 0 && p_right < 64;
	}
	static _ALWAYS_INLINE_ const char *error_message(int64_t p_left, int64_t p_right) {
		if (p_left < 0) {
			return "Invalid operands for bit shifting. Only positive operands are supported.";
		}
		return "Invalid operands for bit shifting. The shift count must be in the range 0...63.";
	}
	// Shifting a positive value into or past the sign bit overflows a signed shift, so the accepted
	// result is produced in the unsigned domain and reinterpreted. `1 << 63` stays the minimum integer.
	static _ALWAYS_INLINE_ int64_t compute(int64_t p_left, int64_t p_right) {
		return variant_int_from_bits(uint64_t(p_left) << uint64_t(p_right));
	}
};

struct VariantIntShiftRightOperation {
	static _ALWAYS_INLINE_ bool accepts(int64_t p_left, int64_t p_right) {
		return p_left >= 0 && p_right >= 0 && p_right < 64;
	}
	static _ALWAYS_INLINE_ const char *error_message(int64_t p_left, int64_t p_right) {
		if (p_left < 0) {
			return "Invalid operands for bit shifting. Only positive operands are supported.";
		}
		return "Invalid operands for bit shifting. The shift count must be in the range 0...63.";
	}
	static _ALWAYS_INLINE_ int64_t compute(int64_t p_left, int64_t p_right) { return p_left >> p_right; }
};

// Every entry point rejects operands the operation cannot evaluate instead of running the undefined
// C++ operation, in every build. `evaluate()` reports the failure through its validity flag; the
// validated and pointer entry points cannot, so they fall back to zero and raise an engine error, the
// same shape the unsigned checked evaluator uses for an unreportable failure.
template <typename Operation>
class OperatorEvaluatorIntCheckedBinary {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		const int64_t left_value = VariantIntCarrier::get(&p_left);
		const int64_t right_value = VariantIntCarrier::get(&p_right);
		if (unlikely(!Operation::accepts(left_value, right_value))) {
			*r_ret = Operation::error_message(left_value, right_value);
			r_valid = false;
			return;
		}
		// The result is computed before the destination is retagged so an aliased destination is safe.
		const int64_t result = Operation::compute(left_value, right_value);
		*r_ret = result;
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const int64_t left_value = VariantIntCarrier::get(left);
		const int64_t right_value = VariantIntCarrier::get(right);
		if (unlikely(!Operation::accepts(left_value, right_value))) {
			VariantInternalAccessor<int64_t>::get(r_ret) = 0;
			ERR_FAIL_MSG(Operation::error_message(left_value, right_value));
		}
		VariantInternalAccessor<int64_t>::get(r_ret) = Operation::compute(left_value, right_value);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		const int64_t left_value = VariantIntCarrier::get_ptr(left);
		const int64_t right_value = VariantIntCarrier::get_ptr(right);
		if (unlikely(!Operation::accepts(left_value, right_value))) {
			PtrToArg<int64_t>::encode(0, r_ret);
			ERR_FAIL_MSG(Operation::error_message(left_value, right_value));
		}
		PtrToArg<int64_t>::encode(Operation::compute(left_value, right_value), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::INT; }
};

template <typename Left, typename Right, typename Operation>
class OperatorEvaluatorIntegerCompare : public CommonEvaluate<OperatorEvaluatorIntegerCompare<Left, Right, Operation>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = Operation::compare(Left::get(left), Right::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(Operation::compare(Left::get_ptr(left), Right::get_ptr(right)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorEqualObject : public CommonEvaluate<OperatorEvaluatorEqualObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Object *a = left->get_validated_object();
		const Object *b = right->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = Variant::object_hash_compare(a, b);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(Variant::object_hash_compare(PtrToArg<Object *>::convert(left), PtrToArg<Object *>::convert(right)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorEqualObjectNil : public CommonEvaluate<OperatorEvaluatorEqualObjectNil> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Object *a = left->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = a == nullptr;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Object *>::convert(left) == nullptr, r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorEqualNilObject : public CommonEvaluate<OperatorEvaluatorEqualNilObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Object *b = right->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = nullptr == b;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(nullptr == PtrToArg<Object *>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorNotEqual : public CommonEvaluate<OperatorEvaluatorNotEqual<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) != VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) != PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotEqualObject : public CommonEvaluate<OperatorEvaluatorNotEqualObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		Object *a = left->get_validated_object();
		Object *b = right->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = !Variant::object_hash_compare(a, b);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(!Variant::object_hash_compare(PtrToArg<Object *>::convert(left), PtrToArg<Object *>::convert(right)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotEqualObjectNil : public CommonEvaluate<OperatorEvaluatorNotEqualObjectNil> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		Object *a = left->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = a != nullptr;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Object *>::convert(left) != nullptr, r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotEqualNilObject : public CommonEvaluate<OperatorEvaluatorNotEqualNilObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		Object *b = right->get_validated_object();
		VariantInternalAccessor<bool>::get(r_ret) = nullptr != b;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(nullptr != PtrToArg<Object *>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorLess : public CommonEvaluate<OperatorEvaluatorLess<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) < VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) < PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorLessEqual : public CommonEvaluate<OperatorEvaluatorLessEqual<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) <= VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) <= PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorGreater : public CommonEvaluate<OperatorEvaluatorGreater<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) > VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) > PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorGreaterEqual : public CommonEvaluate<OperatorEvaluatorGreaterEqual<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) >= VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) >= PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorAnd : public CommonEvaluate<OperatorEvaluatorAnd<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) && VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) && PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorOr : public CommonEvaluate<OperatorEvaluatorOr<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) || VariantInternalAccessor<B>::get(right);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) || PtrToArg<B>::convert(right), r_ret);
	}
	using ReturnType = bool;
};

#define XOR_OP(m_a, m_b) (((m_a) || (m_b)) && !((m_a) && (m_b)))
template <typename A, typename B>
class OperatorEvaluatorXor : public CommonEvaluate<OperatorEvaluatorXor<A, B>> {
public:
	_FORCE_INLINE_ static bool xor_op(const A &a, const B &b) {
		return ((a) || (b)) && !((a) && (b));
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = xor_op(VariantInternalAccessor<A>::get(left), VariantInternalAccessor<B>::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(xor_op(PtrToArg<A>::convert(left), PtrToArg<B>::convert(right)), r_ret);
	}
	using ReturnType = bool;
};

template <typename A>
class OperatorEvaluatorNot : public CommonEvaluate<OperatorEvaluatorNot<A>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<A>::get(left) == A();
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<A>::convert(left) == A(), r_ret);
	}
	using ReturnType = bool;
};

//// CUSTOM ////

class OperatorEvaluatorAddArray : public CommonEvaluate<OperatorEvaluatorAddArray> {
public:
	_FORCE_INLINE_ static void _add_arrays(Array &sum, const Array &array_a, const Array &array_b) {
		int asize = array_a.size();
		int bsize = array_b.size();

		if (array_a.is_typed() && array_a.is_same_typed(array_b)) {
			sum.set_typed(array_a.get_element_type());
		}

		sum.resize(asize + bsize);
		for (int i = 0; i < asize; i++) {
			sum[i] = array_a[i];
		}
		for (int i = 0; i < bsize; i++) {
			sum[i + asize] = array_b[i];
		}
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		*r_ret = Array();
		_add_arrays(VariantInternalAccessor<Array>::get(r_ret), VariantInternalAccessor<Array>::get(left), VariantInternalAccessor<Array>::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		Array ret;
		_add_arrays(ret, PtrToArg<Array>::convert(left), PtrToArg<Array>::convert(right));
		PtrToArg<Array>::encode(ret, r_ret);
	}
	using ReturnType = Array;
};

template <typename T>
class OperatorEvaluatorAppendArray : public CommonEvaluate<OperatorEvaluatorAppendArray<T>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<Vector<T>>::get(r_ret) = VariantInternalAccessor<Vector<T>>::get(left);
		VariantInternalAccessor<Vector<T>>::get(r_ret).append_array(VariantInternalAccessor<Vector<T>>::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		Vector<T> sum = PtrToArg<Vector<T>>::convert(left);
		sum.append_array(PtrToArg<Vector<T>>::convert(right));
		PtrToArg<Vector<T>>::encode(sum, r_ret);
	}
	using ReturnType = Vector<T>;
};

template <typename Left, typename Right>
class OperatorEvaluatorStringConcat : public CommonEvaluate<OperatorEvaluatorStringConcat<Left, Right>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const String a(VariantInternalAccessor<Left>::get(left));
		const String b(VariantInternalAccessor<Right>::get(right));
		VariantInternalAccessor<String>::get(r_ret) = a + b;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		const String a(PtrToArg<Left>::convert(left));
		const String b(PtrToArg<Right>::convert(right));
		PtrToArg<String>::encode(a + b, r_ret);
	}
	using ReturnType = String;
};

template <typename S, typename T>
class OperatorEvaluatorStringFormat;

template <typename S>
class OperatorEvaluatorStringFormat<S, void> {
public:
	_FORCE_INLINE_ static String do_mod(const String &s, bool *r_valid) {
		Array values = { Variant() };
		String a = s.sprintf(values, r_valid);
		if (r_valid) {
			*r_valid = !*r_valid;
		}
		return a;
	}
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		*r_ret = do_mod(VariantInternalAccessor<S>::get(&p_left), &r_valid);
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		bool valid = true;
		String result = do_mod(VariantInternalAccessor<S>::get(left), &valid);
		if (unlikely(!valid)) {
			VariantInternalAccessor<String>::get(r_ret) = VariantInternalAccessor<S>::get(left);
			ERR_FAIL_MSG(vformat("String formatting error: %s.", result));
		}
		VariantInternalAccessor<String>::get(r_ret) = result;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<String>::encode(do_mod(PtrToArg<S>::convert(left), nullptr), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::STRING; }
};

template <typename S>
class OperatorEvaluatorStringFormat<S, Array> {
public:
	_FORCE_INLINE_ static String do_mod(const String &s, const Array &p_values, bool *r_valid) {
		String a = s.sprintf(p_values, r_valid);
		if (r_valid) {
			*r_valid = !*r_valid;
		}
		return a;
	}
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		*r_ret = do_mod(VariantInternalAccessor<S>::get(&p_left), VariantInternalAccessor<Array>::get(&p_right), &r_valid);
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		bool valid = true;
		String result = do_mod(VariantInternalAccessor<S>::get(left), VariantInternalAccessor<Array>::get(right), &valid);
		if (unlikely(!valid)) {
			VariantInternalAccessor<String>::get(r_ret) = VariantInternalAccessor<S>::get(left);
			ERR_FAIL_MSG(vformat("String formatting error: %s.", result));
		}
		VariantInternalAccessor<String>::get(r_ret) = result;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<String>::encode(do_mod(PtrToArg<S>::convert(left), PtrToArg<Array>::convert(right), nullptr), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::STRING; }
};

template <typename S>
class OperatorEvaluatorStringFormat<S, Object> {
public:
	_FORCE_INLINE_ static String do_mod(const String &s, const Object *p_object, bool *r_valid) {
		Array values = { p_object };
		String a = s.sprintf(values, r_valid);
		if (r_valid) {
			*r_valid = !*r_valid;
		}

		return a;
	}
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		*r_ret = do_mod(VariantInternalAccessor<S>::get(&p_left), p_right.get_validated_object(), &r_valid);
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		bool valid = true;
		String result = do_mod(VariantInternalAccessor<S>::get(left), right->get_validated_object(), &valid);
		if (unlikely(!valid)) {
			VariantInternalAccessor<String>::get(r_ret) = VariantInternalAccessor<S>::get(left);
			ERR_FAIL_MSG(vformat("String formatting error: %s.", result));
		}
		VariantInternalAccessor<String>::get(r_ret) = result;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<String>::encode(do_mod(PtrToArg<S>::convert(left), PtrToArg<Object *>::convert(right), nullptr), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::STRING; }
};

template <typename S, typename T>
class OperatorEvaluatorStringFormat {
public:
	_FORCE_INLINE_ static String do_mod(const String &s, const T &p_value, bool *r_valid) {
		Array values = { p_value };
		String a = s.sprintf(values, r_valid);
		if (r_valid) {
			*r_valid = !*r_valid;
		}
		return a;
	}
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		*r_ret = do_mod(VariantInternalAccessor<S>::get(&p_left), VariantInternalAccessor<T>::get(&p_right), &r_valid);
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		bool valid = true;
		String result = do_mod(VariantInternalAccessor<S>::get(left), VariantInternalAccessor<T>::get(right), &valid);
		if (unlikely(!valid)) {
			VariantInternalAccessor<String>::get(r_ret) = VariantInternalAccessor<S>::get(left);
			ERR_FAIL_MSG(vformat("String formatting error: %s.", result));
		}
		VariantInternalAccessor<String>::get(r_ret) = result;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<String>::encode(do_mod(PtrToArg<S>::convert(left), PtrToArg<T>::convert(right), nullptr), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::STRING; }
};

// String formatting for the unsigned carrier. The value is re-boxed as `UINT` explicitly because
// `Variant(uint64_t)` selects the signed carrier and would format the upper half of the range as a
// negative number.
template <typename S>
class OperatorEvaluatorStringFormatUInt {
public:
	_FORCE_INLINE_ static String do_mod(const String &s, uint64_t p_value, bool *r_valid) {
		Array values = { VariantUIntCarrier::box(p_value) };
		String a = s.sprintf(values, r_valid);
		if (r_valid) {
			*r_valid = !*r_valid;
		}
		return a;
	}
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		*r_ret = do_mod(VariantInternalAccessor<S>::get(&p_left), VariantUIntCarrier::get(&p_right), &r_valid);
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		bool valid = true;
		String result = do_mod(VariantInternalAccessor<S>::get(left), VariantUIntCarrier::get(right), &valid);
		if (unlikely(!valid)) {
			VariantInternalAccessor<String>::get(r_ret) = VariantInternalAccessor<S>::get(left);
			ERR_FAIL_MSG(vformat("String formatting error: %s.", result));
		}
		VariantInternalAccessor<String>::get(r_ret) = result;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<String>::encode(do_mod(PtrToArg<S>::convert(left), VariantUIntCarrier::get_ptr(right), nullptr), r_ret);
	}
	static Variant::Type get_return_type() { return Variant::STRING; }
};

class OperatorEvaluatorAlwaysTrue : public CommonEvaluate<OperatorEvaluatorAlwaysTrue> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = true;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(true, r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorAlwaysFalse : public CommonEvaluate<OperatorEvaluatorAlwaysFalse> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = false;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(false, r_ret);
	}
	using ReturnType = bool;
};

///// OR ///////

_FORCE_INLINE_ static bool _operate_or(bool p_left, bool p_right) {
	return p_left || p_right;
}

_FORCE_INLINE_ static bool _operate_and(bool p_left, bool p_right) {
	return p_left && p_right;
}

_FORCE_INLINE_ static bool _operate_xor(bool p_left, bool p_right) {
	return (p_left || p_right) && !(p_left && p_right);
}

_FORCE_INLINE_ static bool _operate_get_nil(const Variant *p_ptr) {
	return p_ptr->get_validated_object() != nullptr;
}

_FORCE_INLINE_ static bool _operate_get_bool(const Variant *p_ptr) {
	return VariantInternalAccessor<bool>::get(p_ptr);
}

_FORCE_INLINE_ static bool _operate_get_int(const Variant *p_ptr) {
	return VariantInternalAccessor<int64_t>::get(p_ptr) != 0;
}

_FORCE_INLINE_ static bool _operate_get_uint(const Variant *p_ptr) {
	return VariantUIntCarrier::get(p_ptr) != 0;
}

_FORCE_INLINE_ static bool _operate_get_float(const Variant *p_ptr) {
	return VariantInternalAccessor<double>::get(p_ptr) != 0.0;
}

_FORCE_INLINE_ static bool _operate_get_object(const Variant *p_ptr) {
	return p_ptr->get_validated_object() != nullptr;
}

_FORCE_INLINE_ static bool _operate_get_ptr_nil(const void *p_ptr) {
	return false;
}

_FORCE_INLINE_ static bool _operate_get_ptr_bool(const void *p_ptr) {
	return PtrToArg<bool>::convert(p_ptr);
}

_FORCE_INLINE_ static bool _operate_get_ptr_int(const void *p_ptr) {
	return PtrToArg<int64_t>::convert(p_ptr) != 0;
}

_FORCE_INLINE_ static bool _operate_get_ptr_uint(const void *p_ptr) {
	return VariantUIntCarrier::get_ptr(p_ptr) != 0;
}

_FORCE_INLINE_ static bool _operate_get_ptr_float(const void *p_ptr) {
	return PtrToArg<double>::convert(p_ptr) != 0.0;
}

_FORCE_INLINE_ static bool _operate_get_ptr_object(const void *p_ptr) {
	return PtrToArg<Object *>::convert(p_ptr) != nullptr;
}

#define OP_EVALUATOR(m_class_name, m_left, m_right, m_op)                                                                 \
	class m_class_name : public CommonEvaluate<m_class_name> {                                                            \
	public:                                                                                                               \
		static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {                \
			VariantInternalAccessor<bool>::get(r_ret) = m_op(_operate_get_##m_left(left), _operate_get_##m_right(right)); \
		}                                                                                                                 \
                                                                                                                          \
		static void ptr_evaluate(const void *left, const void *right, void *r_ret) {                                      \
			PtrToArg<bool>::encode(m_op(_operate_get_ptr_##m_left(left), _operate_get_ptr_##m_right(right)), r_ret);      \
		}                                                                                                                 \
                                                                                                                          \
		using ReturnType = bool;                                                                                          \
	};

// OR

// nil
OP_EVALUATOR(OperatorEvaluatorNilXBoolOr, nil, bool, _operate_or)
OP_EVALUATOR(OperatorEvaluatorBoolXNilOr, bool, nil, _operate_or)

OP_EVALUATOR(OperatorEvaluatorNilXIntOr, nil, int, _operate_or)
OP_EVALUATOR(OperatorEvaluatorIntXNilOr, int, nil, _operate_or)

OP_EVALUATOR(OperatorEvaluatorNilXFloatOr, nil, float, _operate_or)
OP_EVALUATOR(OperatorEvaluatorFloatXNilOr, float, nil, _operate_or)

OP_EVALUATOR(OperatorEvaluatorObjectXNilOr, object, nil, _operate_or)
OP_EVALUATOR(OperatorEvaluatorNilXObjectOr, nil, object, _operate_or)

// bool
OP_EVALUATOR(OperatorEvaluatorBoolXBoolOr, bool, bool, _operate_or)

OP_EVALUATOR(OperatorEvaluatorBoolXIntOr, bool, int, _operate_or)
OP_EVALUATOR(OperatorEvaluatorIntXBoolOr, int, bool, _operate_or)

OP_EVALUATOR(OperatorEvaluatorBoolXFloatOr, bool, float, _operate_or)
OP_EVALUATOR(OperatorEvaluatorFloatXBoolOr, float, bool, _operate_or)

OP_EVALUATOR(OperatorEvaluatorBoolXObjectOr, bool, object, _operate_or)
OP_EVALUATOR(OperatorEvaluatorObjectXBoolOr, object, bool, _operate_or)

// int
OP_EVALUATOR(OperatorEvaluatorIntXIntOr, int, int, _operate_or)

OP_EVALUATOR(OperatorEvaluatorIntXFloatOr, int, float, _operate_or)
OP_EVALUATOR(OperatorEvaluatorFloatXIntOr, float, int, _operate_or)

OP_EVALUATOR(OperatorEvaluatorIntXObjectOr, int, object, _operate_or)
OP_EVALUATOR(OperatorEvaluatorObjectXIntOr, object, int, _operate_or)

// uint
OP_EVALUATOR(OperatorEvaluatorUIntXUIntOr, uint, uint, _operate_or)

OP_EVALUATOR(OperatorEvaluatorNilXUIntOr, nil, uint, _operate_or)
OP_EVALUATOR(OperatorEvaluatorUIntXNilOr, uint, nil, _operate_or)

OP_EVALUATOR(OperatorEvaluatorBoolXUIntOr, bool, uint, _operate_or)
OP_EVALUATOR(OperatorEvaluatorUIntXBoolOr, uint, bool, _operate_or)

OP_EVALUATOR(OperatorEvaluatorIntXUIntOr, int, uint, _operate_or)
OP_EVALUATOR(OperatorEvaluatorUIntXIntOr, uint, int, _operate_or)

OP_EVALUATOR(OperatorEvaluatorUIntXFloatOr, uint, float, _operate_or)
OP_EVALUATOR(OperatorEvaluatorFloatXUIntOr, float, uint, _operate_or)

OP_EVALUATOR(OperatorEvaluatorUIntXObjectOr, uint, object, _operate_or)
OP_EVALUATOR(OperatorEvaluatorObjectXUIntOr, object, uint, _operate_or)

// float
OP_EVALUATOR(OperatorEvaluatorFloatXFloatOr, float, float, _operate_or)

OP_EVALUATOR(OperatorEvaluatorFloatXObjectOr, float, object, _operate_or)
OP_EVALUATOR(OperatorEvaluatorObjectXFloatOr, object, float, _operate_or)

// object
OP_EVALUATOR(OperatorEvaluatorObjectXObjectOr, object, object, _operate_or)

// AND

// nil
OP_EVALUATOR(OperatorEvaluatorNilXBoolAnd, nil, bool, _operate_and)
OP_EVALUATOR(OperatorEvaluatorBoolXNilAnd, bool, nil, _operate_and)

OP_EVALUATOR(OperatorEvaluatorNilXIntAnd, nil, int, _operate_and)
OP_EVALUATOR(OperatorEvaluatorIntXNilAnd, int, nil, _operate_and)

OP_EVALUATOR(OperatorEvaluatorNilXFloatAnd, nil, float, _operate_and)
OP_EVALUATOR(OperatorEvaluatorFloatXNilAnd, float, nil, _operate_and)

OP_EVALUATOR(OperatorEvaluatorObjectXNilAnd, object, nil, _operate_and)
OP_EVALUATOR(OperatorEvaluatorNilXObjectAnd, nil, object, _operate_and)

// bool
OP_EVALUATOR(OperatorEvaluatorBoolXBoolAnd, bool, bool, _operate_and)

OP_EVALUATOR(OperatorEvaluatorBoolXIntAnd, bool, int, _operate_and)
OP_EVALUATOR(OperatorEvaluatorIntXBoolAnd, int, bool, _operate_and)

OP_EVALUATOR(OperatorEvaluatorBoolXFloatAnd, bool, float, _operate_and)
OP_EVALUATOR(OperatorEvaluatorFloatXBoolAnd, float, bool, _operate_and)

OP_EVALUATOR(OperatorEvaluatorBoolXObjectAnd, bool, object, _operate_and)
OP_EVALUATOR(OperatorEvaluatorObjectXBoolAnd, object, bool, _operate_and)

// int
OP_EVALUATOR(OperatorEvaluatorIntXIntAnd, int, int, _operate_and)

OP_EVALUATOR(OperatorEvaluatorIntXFloatAnd, int, float, _operate_and)
OP_EVALUATOR(OperatorEvaluatorFloatXIntAnd, float, int, _operate_and)

OP_EVALUATOR(OperatorEvaluatorIntXObjectAnd, int, object, _operate_and)
OP_EVALUATOR(OperatorEvaluatorObjectXIntAnd, object, int, _operate_and)

// uint
OP_EVALUATOR(OperatorEvaluatorUIntXUIntAnd, uint, uint, _operate_and)

OP_EVALUATOR(OperatorEvaluatorNilXUIntAnd, nil, uint, _operate_and)
OP_EVALUATOR(OperatorEvaluatorUIntXNilAnd, uint, nil, _operate_and)

OP_EVALUATOR(OperatorEvaluatorBoolXUIntAnd, bool, uint, _operate_and)
OP_EVALUATOR(OperatorEvaluatorUIntXBoolAnd, uint, bool, _operate_and)

OP_EVALUATOR(OperatorEvaluatorIntXUIntAnd, int, uint, _operate_and)
OP_EVALUATOR(OperatorEvaluatorUIntXIntAnd, uint, int, _operate_and)

OP_EVALUATOR(OperatorEvaluatorUIntXFloatAnd, uint, float, _operate_and)
OP_EVALUATOR(OperatorEvaluatorFloatXUIntAnd, float, uint, _operate_and)

OP_EVALUATOR(OperatorEvaluatorUIntXObjectAnd, uint, object, _operate_and)
OP_EVALUATOR(OperatorEvaluatorObjectXUIntAnd, object, uint, _operate_and)

// float
OP_EVALUATOR(OperatorEvaluatorFloatXFloatAnd, float, float, _operate_and)

OP_EVALUATOR(OperatorEvaluatorFloatXObjectAnd, float, object, _operate_and)
OP_EVALUATOR(OperatorEvaluatorObjectXFloatAnd, object, float, _operate_and)

// object
OP_EVALUATOR(OperatorEvaluatorObjectXObjectAnd, object, object, _operate_and)

// XOR

// nil
OP_EVALUATOR(OperatorEvaluatorNilXBoolXor, nil, bool, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorBoolXNilXor, bool, nil, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorNilXIntXor, nil, int, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorIntXNilXor, int, nil, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorNilXFloatXor, nil, float, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorFloatXNilXor, float, nil, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorObjectXNilXor, object, nil, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorNilXObjectXor, nil, object, _operate_xor)

// bool
OP_EVALUATOR(OperatorEvaluatorBoolXBoolXor, bool, bool, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorBoolXIntXor, bool, int, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorIntXBoolXor, int, bool, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorBoolXFloatXor, bool, float, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorFloatXBoolXor, float, bool, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorBoolXObjectXor, bool, object, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorObjectXBoolXor, object, bool, _operate_xor)

// int
OP_EVALUATOR(OperatorEvaluatorIntXIntXor, int, int, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorIntXFloatXor, int, float, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorFloatXIntXor, float, int, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorIntXObjectXor, int, object, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorObjectXIntXor, object, int, _operate_xor)

// uint
OP_EVALUATOR(OperatorEvaluatorUIntXUIntXor, uint, uint, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorNilXUIntXor, nil, uint, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorUIntXNilXor, uint, nil, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorBoolXUIntXor, bool, uint, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorUIntXBoolXor, uint, bool, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorIntXUIntXor, int, uint, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorUIntXIntXor, uint, int, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorUIntXFloatXor, uint, float, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorFloatXUIntXor, float, uint, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorUIntXObjectXor, uint, object, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorObjectXUIntXor, object, uint, _operate_xor)

// float
OP_EVALUATOR(OperatorEvaluatorFloatXFloatXor, float, float, _operate_xor)

OP_EVALUATOR(OperatorEvaluatorFloatXObjectXor, float, object, _operate_xor)
OP_EVALUATOR(OperatorEvaluatorObjectXFloatXor, object, float, _operate_xor)

// object
OP_EVALUATOR(OperatorEvaluatorObjectXObjectXor, object, object, _operate_xor)

class OperatorEvaluatorNotBool : public CommonEvaluate<OperatorEvaluatorNotBool> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = !VariantInternalAccessor<bool>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(!PtrToArg<bool>::convert(left), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotInt : public CommonEvaluate<OperatorEvaluatorNotInt> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = !VariantInternalAccessor<int64_t>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(!PtrToArg<int64_t>::convert(left), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotUInt : public CommonEvaluate<OperatorEvaluatorNotUInt> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = !*VariantInternal::get_uint(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(!PtrToArg<uint64_t>::convert(left), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotFloat : public CommonEvaluate<OperatorEvaluatorNotFloat> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = !VariantInternalAccessor<double>::get(left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(!PtrToArg<double>::convert(left), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorNotObject : public CommonEvaluate<OperatorEvaluatorNotObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = left->get_validated_object() == nullptr;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Object *>::convert(left) == nullptr, r_ret);
	}
	using ReturnType = bool;
};

////

template <typename Left, typename Right>
class OperatorEvaluatorInStringFind;

template <typename Left>
class OperatorEvaluatorInStringFind<Left, String> : public CommonEvaluate<OperatorEvaluatorInStringFind<Left, String>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Left &str_a = VariantInternalAccessor<Left>::get(left);
		const String &str_b = VariantInternalAccessor<String>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = str_b.find(str_a) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<String>::convert(right).find(PtrToArg<Left>::convert(left)) != -1, r_ret);
	}
	using ReturnType = bool;
};

template <typename Left>
class OperatorEvaluatorInStringFind<Left, StringName> : public CommonEvaluate<OperatorEvaluatorInStringFind<Left, StringName>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Left &str_a = VariantInternalAccessor<Left>::get(left);
		const String str_b = VariantInternalAccessor<StringName>::get(right).operator String();
		VariantInternalAccessor<bool>::get(r_ret) = str_b.find(str_a) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<StringName>::convert(right).operator String().find(PtrToArg<Left>::convert(left)) != -1, r_ret);
	}
	using ReturnType = bool;
};

template <typename A, typename B>
class OperatorEvaluatorInArrayFind : public CommonEvaluate<OperatorEvaluatorInArrayFind<A, B>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const A &a = VariantInternalAccessor<A>::get(left);
		const B &b = VariantInternalAccessor<B>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = b.find(a) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<B>::convert(right).find(PtrToArg<A>::convert(left)) != -1, r_ret);
	}
	using ReturnType = bool;
};

// Untyped containers compare through `Variant`, which already relates the two integer carriers
// exactly, so unsigned containment forwards the whole operand instead of a narrowed C++ value.
class OperatorEvaluatorInArrayFindUInt : public CommonEvaluate<OperatorEvaluatorInArrayFindUInt> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<Array>::get(right).find(*left) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Array>::convert(right).find(VariantUIntCarrier::box(VariantUIntCarrier::get_ptr(left))) != -1, r_ret);
	}
	using ReturnType = bool;
};

// Packed arrays store a narrower signed or floating element, so an unsigned value outside the element
// range can never be contained. Narrowing it instead would alias onto an unrelated element, letting
// `UINT64_MAX` match a stored `-1`.
template <typename Element, typename B>
class OperatorEvaluatorInPackedArrayFindUInt : public CommonEvaluate<OperatorEvaluatorInPackedArrayFindUInt<Element, B>> {
public:
	_FORCE_INLINE_ static bool contains(uint64_t p_value, const B &p_array) {
		if constexpr (std::is_integral_v<Element>) {
			if (p_value > uint64_t(std::numeric_limits<Element>::max())) {
				return false;
			}
		}
		return p_array.find(Element(p_value)) != -1;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = contains(VariantUIntCarrier::get(left), VariantInternalAccessor<B>::get(right));
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(contains(VariantUIntCarrier::get_ptr(left), PtrToArg<B>::convert(right)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorInArrayFindNil : public CommonEvaluate<OperatorEvaluatorInArrayFindNil> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Array &b = VariantInternalAccessor<Array>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = b.find(Variant()) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Array>::convert(right).find(Variant()) != -1, r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorInArrayFindObject : public CommonEvaluate<OperatorEvaluatorInArrayFindObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Array &b = VariantInternalAccessor<Array>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = b.find(*left) != -1;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Array>::convert(right).find(PtrToArg<Object *>::convert(left)) != -1, r_ret);
	}
	using ReturnType = bool;
};

template <typename A>
class OperatorEvaluatorInDictionaryHas : public CommonEvaluate<OperatorEvaluatorInDictionaryHas<A>> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Dictionary &b = VariantInternalAccessor<Dictionary>::get(right);
		const A &a = VariantInternalAccessor<A>::get(left);
		VariantInternalAccessor<bool>::get(r_ret) = b.has(a);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Dictionary>::convert(right).has(PtrToArg<A>::convert(left)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorInDictionaryHasUInt : public CommonEvaluate<OperatorEvaluatorInDictionaryHasUInt> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		VariantInternalAccessor<bool>::get(r_ret) = VariantInternalAccessor<Dictionary>::get(right).has(*left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Dictionary>::convert(right).has(VariantUIntCarrier::box(VariantUIntCarrier::get_ptr(left))), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorInDictionaryHasNil : public CommonEvaluate<OperatorEvaluatorInDictionaryHasNil> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Dictionary &b = VariantInternalAccessor<Dictionary>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = b.has(Variant());
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Dictionary>::convert(right).has(Variant()), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorInDictionaryHasObject : public CommonEvaluate<OperatorEvaluatorInDictionaryHasObject> {
public:
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		const Dictionary &b = VariantInternalAccessor<Dictionary>::get(right);
		VariantInternalAccessor<bool>::get(r_ret) = b.has(*left);
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		PtrToArg<bool>::encode(PtrToArg<Dictionary>::convert(right).has(PtrToArg<Object *>::convert(left)), r_ret);
	}
	using ReturnType = bool;
};

class OperatorEvaluatorObjectHasPropertyString {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		Object *b = p_right.get_validated_object();
		if (!b) {
			*r_ret = "Invalid base object for 'in'";
			r_valid = false;
			return;
		}

		const String &a = VariantInternalAccessor<String>::get(&p_left);

		bool exist;
		b->get(a, &exist);
		*r_ret = exist;
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		Object *l = right->get_validated_object();
		if (unlikely(!l)) {
			VariantInternalAccessor<bool>::get(r_ret) = false;
			ERR_FAIL_MSG("Invalid base object for 'in'.");
		}
		const String &a = VariantInternalAccessor<String>::get(left);

		bool valid;
		l->get(a, &valid);
		VariantInternalAccessor<bool>::get(r_ret) = valid;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		bool valid;
		PtrToArg<Object *>::convert(right)->get(PtrToArg<String>::convert(left), &valid);
		PtrToArg<bool>::encode(valid, r_ret);
	}
	static Variant::Type get_return_type() { return Variant::BOOL; }
};

class OperatorEvaluatorObjectHasPropertyStringName {
public:
	static void evaluate(const Variant &p_left, const Variant &p_right, Variant *r_ret, bool &r_valid) {
		Object *b = p_right.get_validated_object();
		if (!b) {
			*r_ret = "Invalid base object for 'in'";
			r_valid = false;
			return;
		}

		const StringName &a = VariantInternalAccessor<StringName>::get(&p_left);

		bool exist;
		b->get(a, &exist);
		*r_ret = exist;
		r_valid = true;
	}
	static inline void validated_evaluate(const Variant *left, const Variant *right, Variant *r_ret) {
		Object *l = right->get_validated_object();
		if (unlikely(!l)) {
			VariantInternalAccessor<bool>::get(r_ret) = false;
			ERR_FAIL_MSG("Invalid base object for 'in'.");
		}
		const StringName &a = VariantInternalAccessor<StringName>::get(left);

		bool valid;
		l->get(a, &valid);
		VariantInternalAccessor<bool>::get(r_ret) = valid;
	}
	static void ptr_evaluate(const void *left, const void *right, void *r_ret) {
		bool valid;
		PtrToArg<Object *>::convert(right)->get(PtrToArg<StringName>::convert(left), &valid);
		PtrToArg<bool>::encode(valid, r_ret);
	}
	static Variant::Type get_return_type() { return Variant::BOOL; }
};
