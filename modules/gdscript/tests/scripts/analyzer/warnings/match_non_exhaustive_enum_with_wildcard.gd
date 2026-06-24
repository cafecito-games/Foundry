enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		_:
			print("other")
	print("ok")
