extends Node
func compute(a, b):
	var x = a + b * 2
	var y = (a + b) * 2
	var z = a and b or not a
	return x == y and z != 0


func ratios():
	var r = 10 / 2 - 3
	return -r * 2
