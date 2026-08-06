# Nested argument descriptors are compared recursively and invariantly: typed containers, nested
# specializations, and `Type[T]` argument layers all participate. The comparison stays shallow with
# respect to the value itself -- it reads the reified descriptor, never the stored contents -- so an
# empty container argument and a populated one answer alike.
class Crate[T]:
	var item: T


class Empty[T]:
	pass


func test() -> void:
	var int_array_crate: Variant = Crate[Array[int]].new()
	var string_array_crate: Variant = Crate[Array[String]].new()

	print("array crate is Crate[Array[int]]: ", int_array_crate is Crate[Array[int]])
	print("array crate is Crate[Array[String]]: ", int_array_crate is Crate[Array[String]])
	print("array crate is Crate[Array]: ", int_array_crate is Crate[Array])
	print("string array crate is Crate[Array[int]]: ", string_array_crate is Crate[Array[int]])

	var dictionary_crate: Variant = Crate[Dictionary[String, int]].new()
	print("dictionary crate matches: ", dictionary_crate is Crate[Dictionary[String, int]])
	print("dictionary crate rejects swapped: ", dictionary_crate is Crate[Dictionary[int, String]])

	var nested_crate: Variant = Crate[Crate[int]].new()
	print("nested crate matches: ", nested_crate is Crate[Crate[int]])
	print("nested crate rejects inner mismatch: ", nested_crate is Crate[Crate[String]])
	print("nested crate is Crate: ", nested_crate is Crate)

	var handle_argument_crate: Variant = Crate[Type[Node]].new()
	print("handle argument matches: ", handle_argument_crate is Crate[Type[Node]])
	print("handle argument rejects instance argument: ", handle_argument_crate is Crate[Node])

	var filled := Crate[Array[int]].new()
	filled.item = [1, 2, 3]
	var populated: Variant = filled
	var drained: Variant = Crate[Array[int]].new()
	print("populated matches: ", populated is Crate[Array[int]])
	print("empty matches the same way: ", drained is Crate[Array[int]])
	print("contents do not change the answer: ", (populated is Crate[Array[int]]) == (drained is Crate[Array[int]]))

	var empty_object: Variant = Empty[int].new()
	print("member-less object matches: ", empty_object is Empty[int])
	print("member-less object rejects mismatch: ", empty_object is Empty[String])

	print("is/as nested arguments ok")
