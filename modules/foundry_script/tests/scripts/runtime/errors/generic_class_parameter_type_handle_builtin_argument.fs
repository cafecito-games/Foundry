# `Type[int]` has no class-handle value at all, so the only value a `Type[T]` slot can hold on a
# `Box[int]` receiver is null. A class handle for some unrelated class is rejected rather than
# slipping through a slot whose represented type is not object-shaped.
class Box[T]:
	func keep_handle(value) -> Type[T]:
		var kept: Type[T] = value
		return kept


func test() -> void:
	print(Box[int].new().keep_handle(Node))
