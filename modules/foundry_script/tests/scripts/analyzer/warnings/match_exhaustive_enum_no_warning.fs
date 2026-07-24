enum Direction:
	NORTH = 0
	EAST = NORTH + 1
	SOUTH = EAST + 1
	WEST = SOUTH + 1

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
