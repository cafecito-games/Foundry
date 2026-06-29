# Calling a method on a specialized instance substitutes the receiver's type arguments into the
# method's parameter types, so an argument of the wrong concrete type is rejected. Here `Box[int]`
# specializes `set_value(v: T)` to `set_value(v: int)`, so passing a String is an error rather than
# being leniently accepted against the bare type parameter `T`.
class Box[T]:
	func set_value(value: T) -> void:
		pass


func test() -> void:
	var b: Box[int]
	b.set_value("x")
