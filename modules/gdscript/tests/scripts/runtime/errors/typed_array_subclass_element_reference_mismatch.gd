# Binding a typed `Array[StringBox]` to an `Array[Box[int]]` parameter is rejected: projecting the
# source element's `StringBox extends Box[String]` specialization onto the expected `Box` base yields
# `Box[String]`, which conflicts invariantly with `Box[int]`. Routing through `Variant` defeats the
# static check and reaches the runtime `can_reference` path for an already-typed source array.
class Box[T]:
	var value: T


class StringBox extends Box[String]:
	pass


func take(boxes: Array[Box[int]]) -> void:
	print(boxes.size())


func test() -> void:
	var strings: Array[StringBox] = [StringBox.new()]
	var erased: Variant = strings
	@warning_ignore("unsafe_call_argument")
	take(erased)
	print("not ok")
