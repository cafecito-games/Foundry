func test():
	# The folded value leaves the promoted range, so it may not claim that width.
	var values: Array[uint] = [4000000000U + 4000000000U]
	print(values)
