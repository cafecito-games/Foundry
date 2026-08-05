# Case construction converts each argument with the already-specialized payload field type, so a
# `Result[Array[int], String]` payload reaches the value as a typed `Array[int]` rather than an
# untyped Array. Containers of specialized unions keep their own element descriptors alongside.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func erase(value: Variant) -> Variant:
	return value

func typed_payload() -> Result[Array[int], String]:
	return Result[Array[int], String].Ok([1, 2])

func collect() -> Array[Result[int, String]]:
	var values: Array[Result[int, String]] = []
	values.append(Result[int, String].Ok(1))
	values.append(Result[int, String].Err("two"))
	return values

func index() -> Dictionary[String, Result[int, String]]:
	var table: Dictionary[String, Result[int, String]] = {}
	table["ok"] = Result[int, String].Ok(3)
	return table

func test():
	# Read the payload back through `Variant` so the typing observed is the value's own, not one a
	# typed destination slot would have converted it into.
	var erased: Variant = erase(typed_payload())
	print(erased)
	@warning_ignore("unsafe_method_access")
	print(erased[1].get_typed_builtin() == TYPE_INT)

	var values := collect()
	print(values.size())
	print(values[0])
	print(values[1])
	print(values.get_typed_builtin() == TYPE_ARRAY)

	var table := index()
	print(table.size())
	print(table["ok"])
	print(table.get_typed_key_builtin() == TYPE_STRING)
	print(table.get_typed_value_builtin() == TYPE_ARRAY)
