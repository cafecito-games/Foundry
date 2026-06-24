enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	var other := Direction.WEST
	match direction:
		Direction.NORTH:
			print("north")
		other:
			print("matched other")
	print("ok")
