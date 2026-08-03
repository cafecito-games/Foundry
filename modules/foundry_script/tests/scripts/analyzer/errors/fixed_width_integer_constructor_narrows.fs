tuple Bounds(low: uint, high: ulong)

func test():
	var wide: ulong = 4UL
	print(Bounds(wide, wide))
