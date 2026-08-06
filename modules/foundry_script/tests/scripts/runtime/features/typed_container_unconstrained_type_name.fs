# Regression guard for the `NumericType::NONE` path in `_get_element_type()`: a container element
# slot that declares no width must keep working exactly as it did before widths existed. `Array[int]`
# is the one integer spelling with no declared width (the `int` carve-out from #1684), so it exercises
# the same fallthrough a `uint`/`ulong`/`long` slot uses without ever taking the numeric-width branch.
func test():
	var ints: Array[int] = [1, 2, 3]
	print(ints)

	var strings: Array[String] = ["a", "b"]
	print(strings)

	var untyped: Array = [1, "two", 3.0]
	print(untyped)

	var lookup: Dictionary[String, Variant] = { "a": 1, "b": "two" }
	print(lookup)
