class Box[T]:
	var value: T


class Pair[A, B] extends Box[A]:
	var other: B


func return_pair_as_int_box_type() -> Type[Box[int]]:
	return Pair[int, String]


func test() -> void:
	var from_widened_pair: Variant = return_pair_as_int_box_type().new()
	from_widened_pair.other = 123
	print("not ok")
