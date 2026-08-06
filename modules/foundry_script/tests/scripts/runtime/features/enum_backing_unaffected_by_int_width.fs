# Enum values stay signed 64-bit and must not adopt `int`'s new 32-bit width: a value outside the
# 32-bit signed range still round-trips through an enum, through the Variant it erases to, and through
# an `int`-typed native carrier position that only cares about the carrier.
enum WideValues:
	BELOW_INT_MIN = -2147483649
	ABOVE_INT_MAX = 2147483648

func test():
	print(WideValues.BELOW_INT_MIN)
	print(WideValues.ABOVE_INT_MAX)

	var erased: Variant = WideValues.ABOVE_INT_MAX
	print(erased)
	print(typeof(erased) == TYPE_INT)
	print(erased is long)
	print(erased is int)
