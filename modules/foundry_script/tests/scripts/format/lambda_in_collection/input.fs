func test():
	other(
		func():
			var x = 5
			return x,
		1,
	)
	var arr = [
		func():
			return 1,
		2,
	]
	other(
		func():
			pass
	)
	return arr
