# A `uses` clause in a declaration header naming a generic trait must spell its type arguments.
trait Storage[T]:
	func stored(value: T) -> T:
		return value


class C uses Storage:
	pass


func test() -> void:
	pass
