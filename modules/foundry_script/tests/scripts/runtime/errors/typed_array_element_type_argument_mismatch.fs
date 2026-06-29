# Appending a differently specialized element (`Box[String]`) into an `Array[Box[int]]` is rejected at
# runtime: element type arguments are invariant, so the reified `String` argument conflicts with the
# expected `int`. The funnel through an untyped `Variant` defeats the static check, exercising the
# runtime element validation path.
class Box[T]:
	var value: T


func make_string_box() -> Variant:
	return Box[String].new()


func test() -> void:
	var boxes: Array[Box[int]] = []
	boxes.append(make_string_box())
	print("not ok")
