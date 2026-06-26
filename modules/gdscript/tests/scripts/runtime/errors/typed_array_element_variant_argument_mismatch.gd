# An explicit `Variant` type argument is definite evidence, not the absence of one: a `Box[Variant]`
# is distinct from `Box[int]` and is rejected from an `Array[Box[int]]`. Guards against treating the
# erased `Variant` argument as an unbound slot (which would falsely accept it). The funnel through
# `Variant` defeats the static check.
class Box[T]:
	var value: T


func make_variant_box() -> Variant:
	return Box[Variant].new()


func test() -> void:
	var boxes: Array[Box[int]] = []
	boxes.append(make_variant_box())
	print("not ok")
