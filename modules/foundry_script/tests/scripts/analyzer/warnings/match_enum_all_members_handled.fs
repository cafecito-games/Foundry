# Handling every declared member silences NON_EXHAUSTIVE_MATCH. A plain enum is still an open,
# integer-backed domain, so MATCH_WITHOUT_DEFAULT remains.
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
