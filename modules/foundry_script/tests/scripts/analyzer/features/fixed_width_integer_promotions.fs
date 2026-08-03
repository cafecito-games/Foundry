tuple Bounds(low: uint, high: ulong)

enum Reading:
	Sample(value: uint)

func take_uint(value: uint) -> uint:
	return value

signal reported(total: ulong)

func test():
	var u: uint = 2U
	var l: long = 3L

	# Each annotated destination is the assertion: a result of any other width is a compile error.
	var same_long: long = l + l

	# uint widens into ulong without a conversion, because every uint value is a ulong value.
	var widened: ulong = u

	# A constant may enter a narrower slot when its exact value fits.
	var narrowed_constant: uint = 4000000000UL
	var exact_float: float = 4L

	# Every typed boundary uses the same rule, so the same constant crosses each of them.
	var elements: Array[uint] = [4000000000UL]
	var passed: uint = take_uint(4000000000UL)
	reported.emit(4UL)
	var sample = Reading.Sample(4000000000UL)
	var bounds = Bounds(4000000000UL, 4UL)

	print(same_long)
	print(widened)
	print(narrowed_constant)
	print(exact_float)
	print(elements[0])
	print(passed)
	print(sample)
	print(bounds)
