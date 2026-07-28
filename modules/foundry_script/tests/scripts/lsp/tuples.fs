class_name LspTuples

## Player position in the world.
tuple PlayerWorldPosition(vec: Vector2i, zone: int)

tuple Pair(int, String)

func locate() -> PlayerWorldPosition:
	return PlayerWorldPosition(Vector2i(1, 2), 3)
