# Every `uint` value is exactly representable as a `double`, so it may widen to `float` implicitly
# just like `int` already does (design section 6.1). `ulong` cannot make that unconditional promise
# above 2^53, so it still needs an explicit cast unless the source is a constant whose exact value
# survives the round trip.
func widen(p_value: uint) -> float:
	return p_value

func test():
	var from_variable: uint = 4294967295U
	var widened: float = from_variable
	print(widened)

	var from_constant: float = 7U
	print(from_constant)

	print(widen(1U))

	var from_zero: uint = 0U
	var widened_zero: float = from_zero
	print(widened_zero)
