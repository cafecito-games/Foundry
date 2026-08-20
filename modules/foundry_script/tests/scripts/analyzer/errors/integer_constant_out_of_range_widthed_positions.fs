func take_long(v: long) -> long:
	return v

func give_long() -> long:
	return 18446744073709551615UL

func test():
	take_long(18446744073709551615UL)
	var v: long = 18446744073709551615UL
	print(v)
	print(give_long())
