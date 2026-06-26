# `is`/`as` against a generic class test the (erased) script identity, not the reified type
# arguments. A `Box[int]` instance satisfies `is Box`, `is Box[int]`, AND `is Box[String]`, and can
# be cast with `as` to any specialization. Type arguments are reified onto the instance for member
# validation and reflection, but are erased for runtime `is`/`as` type tests.
class Box[T]:
	var value: T


func test() -> void:
	var int_box := Box[int].new()
	int_box.value = 5
	var dynamic: Variant = int_box

	print(dynamic is Box)
	print(dynamic is Box[int])
	print(dynamic is Box[String])

	var as_unspecialized := dynamic as Box
	print(as_unspecialized.value)

	var as_int := dynamic as Box[int]
	print(as_int.value)

	var as_string := dynamic as Box[String]
	print(as_string != null)

	print("is/as specialized ok")
