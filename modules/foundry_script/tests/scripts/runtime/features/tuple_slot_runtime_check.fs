# A tuple slot now checks its own declared shape at every function-body boundary, and the check is
# exactly the structural test an `is` uses: matching arity, per-element type test, null accepted only
# where the declaration is nullable. Nothing is converted, and an element the compiler cannot describe
# accepts any value, so no program that was valid before becomes an error.
tuple Vec2(x: float, y: float)


func supply(value: Variant) -> Variant:
	return value


# The check must not retype the value: a tuple's runtime carrier is a read-only, *untyped* Array, and
# that identity is what its value semantics rest on. Reaching the carrier takes a deliberate unsafe
# cast, since a tuple type is not an Array type to the analyzer.
func report_carrier(value: Variant) -> void:
	var carrier := value as Array
	print(carrier.is_read_only())
	print(carrier.is_typed())


func make_pair() -> (int, String):
	return supply((7, "seven"))


func erased_element[T](_witness: T, contents: Variant) -> (int, Array[T]):
	# `Array[T]` erases to an untyped Array, so the element accepts any Array at runtime while the
	# `int` beside it is still enforced.
	return supply((1, contents))


func test() -> void:
	# A matching value passes and is stored unchanged: still the read-only Array a tuple value is,
	# never retyped into a typed Array.
	var pair: (int, String) = supply((1, "one"))
	print(pair)
	print(pair.0)
	print(pair.1)
	print(pair == (1, "one"))
	print(pair is (int, String))
	report_carrier(pair)

	# A later store into the same local goes through the same check.
	pair = supply((2, "two"))
	print(pair)

	# A nullable tuple slot accepts null and a matching tuple alike.
	var maybe: (int, String)? = supply(null)
	print(maybe == null)
	maybe = supply((3, "three"))
	print(maybe)

	# Nesting recurses: the inner tuple is tested as a tuple, not as a bare Array.
	var nested: (int, (String, bool)) = supply((4, ("four", true)))
	print(nested)

	# A named tuple's identity is erased, so a `Vec2` value satisfies a structurally equal unnamed slot.
	var unnamed: (float, float) = supply(Vec2(1.5, 2.5))
	print(unnamed)

	print(make_pair())
	# `T` is `int` here, yet the erased element still takes an Array of anything at all.
	print(erased_element(0, ["not an int", true]))
	print(erased_element(0, []))
