trait Storage[T]:
	func stored(value: T) -> T:
		return value


trait Wrapper:
	uses Storage[int]


class C:
	uses Wrapper, Storage


func take(s: Storage[String]) -> void:
	print(s != null)


func test() -> void:
	take(C.new())
