# `Array.size()` genuinely returns a 32-bit `int` in the native carrier (see `core/variant/array.h`),
# so it does not auto-satisfy a trait requirement of `size() -> long`. The witness comparison recovers
# the builtin method's real declared width from its `MethodInfo` metadata (not a carrier-erased
# wildcard), so this genuine mismatch is still rejected rather than silently accepted.
extend Array uses RtcBuiltinWideSize:
	func tag() -> String:
		return "array"
