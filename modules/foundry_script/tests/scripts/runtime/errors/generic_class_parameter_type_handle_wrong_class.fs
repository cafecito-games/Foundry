# A `Type[T]` slot on a receiver whose argument is object-shaped still checks which class the handle
# denotes, not merely that the value is some handle.
class Box[T]:
	func keep_handle(value) -> Type[T]:
		var kept: Type[T] = value
		return kept


func test() -> void:
	print(Box[Node].new().keep_handle(RefCounted))
