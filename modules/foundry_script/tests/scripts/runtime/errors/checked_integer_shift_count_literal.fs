func shift(count: uint) -> uint:
	return 1U << count

func test():
	print(shift(32U))
