# Handling every declared member and `null` silences NON_EXHAUSTIVE_MATCH. A plain enum is still an
# open, integer-backed domain, so MATCH_WITHOUT_DEFAULT remains.
enum Direction:
	NORTH = 0
	SOUTH = NORTH + 1

func test():
	var direction: Direction? = Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.SOUTH:
			print("south")
		null:
			print("null")
	print("ok")
