class Box[T]:
	func make(value: T) -> (T, int):
		return (value, 0)

func test():
	var box := Box[String].new()
	var wrong: (int, int) = box.make("one")
	print(wrong)
