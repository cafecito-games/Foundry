enum X:
	A = 1
	B = A + (
		2
	)
	# trailing enum comment
enum Shape:
	Circle(radius: float)
	Square(
		side: float,
	)
	# trailing payload comment
