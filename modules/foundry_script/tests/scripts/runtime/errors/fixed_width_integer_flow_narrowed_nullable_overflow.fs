func increment(value: uint?):
	if value == null:
		return null
	return value + 1U

func test():
	print(increment(4294967295U))
