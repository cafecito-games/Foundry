enum Direction { NORTH, SOUTH }

func test():
	var direction: Direction? = Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.SOUTH:
			print("south")
	print("ok")
