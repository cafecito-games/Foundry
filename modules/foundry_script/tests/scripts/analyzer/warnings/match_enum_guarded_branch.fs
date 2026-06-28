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
		_ when direction == Direction.WEST:
			print("guarded west")
	print("ok")
