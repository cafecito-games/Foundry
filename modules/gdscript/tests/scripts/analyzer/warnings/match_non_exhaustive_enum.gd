enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.SOUTH:
			print("south")
	print("ok")
