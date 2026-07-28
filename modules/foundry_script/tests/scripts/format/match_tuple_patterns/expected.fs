func test():
	var point: (int, String) = (1, "a")
	match point:
		(0, "a"):
			print("zero a")
		(var x, var label):
			print(label)
	match ((1, 2), 3):
		((var a, var b), var c):
			prints(a, b, c)
		_:
			pass
