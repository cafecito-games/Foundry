trait Storage[T]:
	func stored(value: T) -> T:
		return value


trait Wrapper[U]:
	uses Storage[U]


class C:
	uses Wrapper[float]


func test() -> void:
	print(C.new().stored(1.5))
