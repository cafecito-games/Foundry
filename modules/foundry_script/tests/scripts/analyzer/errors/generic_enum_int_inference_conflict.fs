enum Axis {
	NORTH = 0,
	SOUTH = 1,
}

func pick[T](a: T, b: T) -> T:
	return a

func test() -> void:
	pick(0, Axis.SOUTH)
	print("not ok")
