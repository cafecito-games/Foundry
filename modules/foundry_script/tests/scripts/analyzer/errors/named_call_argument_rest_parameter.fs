func collect(first: int, ...rest: Array) -> long:
	return first + rest.size()

func test():
	print(collect(1, rest = 2))
