const BIG: Variant = 5000000000

func take_int(v: int) -> int:
	return v

func test():
	take_int(BIG)
	var initialized: int = BIG
	print(initialized)
