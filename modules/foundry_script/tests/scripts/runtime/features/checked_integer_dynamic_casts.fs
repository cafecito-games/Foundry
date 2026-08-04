# A dynamically typed source is converted through the same checked helper as a statically typed one,
# and a source the integer model does not describe keeps the generic conversion it always had.
func widen(value) -> long:
	return value as long

func to_unsigned(value) -> ulong:
	return value as ulong

func test():
	print(widen(9223372036854775807UL))
	print(widen(-5))
	print(widen(3.9))
	print(widen("42"))
	print(widen(true))
	print(to_unsigned(7))
