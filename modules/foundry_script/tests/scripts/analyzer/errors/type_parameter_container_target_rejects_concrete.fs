# Wrapping an erased type parameter in a container creates no runtime evidence: `Array[T]` compiles
# to a plain untyped Array, so a concrete `Array[int]` stored there is never element-checked and
# comes back out as the `Array[T]` the callee trusts. The rejection therefore walks the whole
# destination shape -- array elements, both dictionary slots, and deeper mixed nesting -- instead of
# looking only at the outermost type.
func array_local[T]() -> Array[T]:
	var concrete: Array[int] = [1, 2]
	var local: Array[T] = concrete
	return local


func array_return[T](values: Array[int]) -> Array[T]:
	return values


func array_later_assignment[T](values: Array[int]) -> void:
	var local: Array[T] = []
	local = values
	print(local)


func dictionary_value[T](values: Dictionary[String, int]) -> Dictionary[String, T]:
	return values


func dictionary_key[T](values: Dictionary[int, String]) -> Dictionary[T, String]:
	return values


func mixed_nesting[T](values: Array[Dictionary[String, Array[int]]]) -> Array[Dictionary[String, Array[T]]]:
	return values


func test():
	print(array_local[int]())
	print(array_return[int]([1]))
	array_later_assignment[int]([1])
	print(dictionary_value[int]({}))
	print(dictionary_key[int]({}))
	print(mixed_nesting[int]([]))
