class Box[T]:
	func pick(first: T, second: T = 0, count: int = 1) -> T:
		return first if count > 0 else second

func test():
	# `second` depends on the class type parameter `T`, so its default would be validated against
	# the receiver's substituted type argument; it must be passed explicitly.
	var box := Box[String].new()
	print(box.pick("x", count = 2))
