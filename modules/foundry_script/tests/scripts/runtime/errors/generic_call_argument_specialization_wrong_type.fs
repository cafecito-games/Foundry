# The call-site check decides membership through the one shared structural relation, so a substituted
# specialization is compared by its reified type arguments rather than by its bare script. A
# `Box[int]` is not a `Box[String]`, and the erased destination never got to say so.
class Box[T]:
	var value: T


func identity[T](value: T) -> T:
	print("callee entered")
	return value


func untyped_int_box() -> Variant:
	return Box[int].new()


func test() -> void:
	var wrong: Box[String] = identity[Box[String]](untyped_int_box())
	print(wrong)
