func widen(value) -> long:
	return value as long

func test():
	print(widen(18446744073709551615UL))
