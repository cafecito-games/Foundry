# Method type arguments are not reified in the call frame, so a rest element that depends on one is
# statically specialized at the call site but packed into a plain untyped Array at runtime. A rest
# element that does not depend on a method type parameter stays runtime-reified.
func collect[T](...values: Array[T]) -> Array[T]:
	# The mechanical proof of the erasure boundary: inside the generic body the packed rest array
	# carries no element metadata, even though the caller statically sees `Array[int]`.
	Utils.check(not values.is_typed())
	return values


func collect_ints[T](_witness: T, ...values: Array[int]) -> int:
	# The element type is concrete, so it is reified even inside a generic method.
	Utils.check(values.is_typed())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	return values.size()


func test() -> void:
	var ints: Array[int] = collect(1, 2, 3)
	Utils.check(ints == [1, 2, 3])
	# The caller-side assignment retypes the erased generic return, so the local is genuinely typed.
	Utils.check(ints.is_typed())
	Utils.check(ints.get_typed_builtin() == TYPE_INT)

	var empty: Array[int] = collect[int]()
	Utils.check(empty.is_empty())
	Utils.check(empty.is_typed())

	var texts: Array[String] = collect("a", "b")
	Utils.check(texts == ["a", "b"])

	Utils.check(collect_ints("witness", 4, 5) == 2)
	print("ok")
