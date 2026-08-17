# A method-scope type parameter is chosen per call and erased before the callee runs, so no run-time
# type survives for a store into such a slot to be checked against. A gradual source -- a `Variant` or
# any other non-hard value -- is therefore refused exactly where a concrete one is: gradual means
# "checked at the destination boundary at run time", and this destination emits no check at all.
func bare_local[T](value: Variant) -> void:
	var kept: T = value
	print(kept)


func later_store[T](seed: T, value: Variant) -> void:
	var kept: T = seed
	kept = value
	print(kept)


func gradual_return[T](value: Variant) -> T:
	return value


func array_local[T](value: Variant) -> void:
	var kept: Array[T] = value
	print(kept)


func dictionary_local[T](value: Variant) -> void:
	var kept: Dictionary[String, T] = value
	print(kept)


# A weak (non-hard) source emits no check either, so it is refused on the same grounds. It takes a
# different analyzer branch from a `Variant` one at both the declaration and the later store.
func weak_source[T](value) -> void:
	var kept: T = value
	print(kept)


# A weak source keeps its inferred type, which is how this case is told apart from the `Variant` one:
# the store is refused for lacking a check, not for the value being untyped.
func loose_int():
	return 1


func weak_later_store[T](seed: T) -> void:
	var kept: T = seed
	kept = loose_int()
	print(kept)


# A container literal is retyped to the declared container, so its elements reach the parameter slot
# one at a time rather than as one gradual value. A concrete element is already refused there; a
# gradual one has to be refused on the same grounds, or the bracket would be all it takes to get
# around the rejection above.
func array_literal_element[T](value: Variant) -> void:
	var kept: Array[T] = [value]
	print(kept)


func dictionary_literal_value[T](value: Variant) -> void:
	var kept: Dictionary[String, T] = {"a": value}
	print(kept)


func dictionary_literal_key[T](value: Variant) -> void:
	var kept: Dictionary[T, String] = {value: "a"}
	print(kept)


# A tuple destination is tested element by element, so an element naming the parameter directly is
# refused like a bare one. An element that is a container declared around the parameter is not: its
# runtime element typing belongs to its concrete consumer, and a concrete value is accepted there, so
# a gradual one stays accepted too. See `runtime/features/tuple_slot_runtime_check.fs`.
func tuple_element[T](value: Variant) -> void:
	var kept: (int, T) = value
	print(kept)


func weak_literal_element[T]() -> void:
	var kept: Array[T] = [loose_int()]
	print(kept)


func test():
	bare_local[int]("nope")
	later_store[int](1, "nope")
	print(gradual_return[int]("nope"))
	array_local[int]([1, 2])
	dictionary_local[int]({"a": 1})
	weak_source[int]("nope")
	weak_later_store[int](1)
	array_literal_element[int]("nope")
	dictionary_literal_value[int]("nope")
	dictionary_literal_key[int]("nope")
	tuple_element[int]((1, 2))
	weak_literal_element[int]()
