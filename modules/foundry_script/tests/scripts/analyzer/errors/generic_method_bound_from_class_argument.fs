# A method type parameter bounded by a class type parameter (`keep[U: T]`) sees the class's
# concrete type argument, so the bound on `U` is specialized to the receiver's `T`.
class Box[T: Resource]:
	func keep[U: T](value: U) -> U:
		return value


func test(box: Box[PackedScene], resource: Resource) -> void:
	# `U` is bounded by `T`, which is `PackedScene` here, so a plain `Resource` violates it.
	var bad := box.keep(resource)
	print(bad)
