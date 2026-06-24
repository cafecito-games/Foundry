enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.EAST:
			print("east")
		Direction.SOUTH:
			print("south")
		Direction.WEST:
			print("west")
	print("ok")
