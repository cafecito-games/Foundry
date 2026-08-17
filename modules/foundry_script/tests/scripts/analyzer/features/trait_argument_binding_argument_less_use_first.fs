trait Storage[T]:
	func stored(value: T) -> T:
		return value


trait Wrapper:
	uses Storage[int]


class C:
	uses Storage, Wrapper


func test() -> void:
	var v: Variant = "x"
	print(C.new().stored(v))
