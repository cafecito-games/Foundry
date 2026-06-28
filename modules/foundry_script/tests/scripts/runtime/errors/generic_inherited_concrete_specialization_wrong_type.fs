# A dynamic wrong-typed write to a member inherited from a concretely specialized base
# (`IntBox extends Box[int]`) is rejected against the fixed `int` argument, even though the instance
# carries no reified type arguments of its own.
class Box[T]:
	var value: T


class IntBox extends Box[int]:
	pass


func test() -> void:
	var box := IntBox.new()
	var dynamic: Variant = box
	dynamic.value = "not an int"
	print("unreached")
