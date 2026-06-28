# A non-generic subclass that fixes a concrete specialization (`StringBox extends Box[String]`) is a
# `Box[String]`, even though its instance carries no reified type arguments of its own. Projecting the
# subclass's `extends Box[String]` onto the base lets the runtime reject it from an `Array[Box[int]]`
# instead of accepting it under gradual leniency. The funnel through `Variant` defeats the static check.
class Box[T]:
	var value: T


class StringBox extends Box[String]:
	pass


func make_string_box() -> Variant:
	return StringBox.new()


func test() -> void:
	var boxes: Array[Box[int]] = []
	boxes.append(make_string_box())
	print("not ok")
