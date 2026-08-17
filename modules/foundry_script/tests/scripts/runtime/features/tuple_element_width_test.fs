# A tuple element's declared integer width is part of the structural test. A value on the right
# carrier whose magnitude the declared width cannot hold is not a value of that tuple type, so the
# `is` answer tracks the declaration and not just the carrier. Only a declared width narrows: the
# same value tested against a wider element still matches, and a non-integer element is untouched.
func supply(value: Variant) -> Variant:
	return value


func test():
	var in_range: Variant = supply([7, "x"])
	print(in_range is (int, String))

	var wide: Variant = supply([9223372036854775807, "x"])
	print(wide is (int, String))
	print(wide is (long, String))

	var at_edge: Variant = supply([2147483647, "x"])
	print(at_edge is (int, String))
	var past_edge: Variant = supply([2147483648, "x"])
	print(past_edge is (int, String))
	var at_low_edge: Variant = supply([-2147483648, "x"])
	print(at_low_edge is (int, String))
	var below_low_edge: Variant = supply([-2147483649, "x"])
	print(below_low_edge is (int, String))

	# The unsigned carrier keeps its own boundary, and a negative value fails on the carrier alone.
	var negative: Variant = supply([-1, "x"])
	print(negative is (uint, String))
	var unsigned_in_range: Variant = supply([4294967295U, "x"])
	print(unsigned_in_range is (uint, String))
	var unsigned_past_edge: Variant = supply([4294967296UL, "x"])
	print(unsigned_past_edge is (uint, String))
	print(unsigned_past_edge is (ulong, String))

	# The width travels through nesting: only the inner element overflows here.
	var nested_overflow: Variant = supply([1, [9223372036854775807, "x"]])
	print(nested_overflow is (int, (int, String)))
	var nested_in_range: Variant = supply([1, [2, "x"]])
	print(nested_in_range is (int, (int, String)))

	# An element that declares no width is constrained by its carrier alone, exactly as before.
	var floating: Variant = supply([1.5, "x"])
	print(floating is (float, String))
	print(floating is (int, String))
