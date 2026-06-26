# A generic method returning a typed container whose element is a type parameter (`-> Array[T]`)
# yields an untyped array at runtime (the element is erased). Assigning that result to a concrete
# typed container retypes it, so the target is a genuinely typed `Array[int]`. This holds for a typed
# declaration, a later assignment, and nested element types; assigning to an untyped `Array` target
# keeps the untyped value (no regression).
func singleton[T](value: T) -> Array[T]:
	var result: Array[T] = []
	result.append(value)
	return result


func nested[T](value: T) -> Array[Array[T]]:
	return [[value]]


func test() -> void:
	var wrapped: Array[int] = singleton(7)
	print(wrapped, " ", wrapped.get_typed_builtin() == TYPE_INT)

	var reassigned: Array[int]
	reassigned = singleton(5)
	print(reassigned, " ", reassigned.get_typed_builtin() == TYPE_INT)

	var deep: Array[Array[int]] = nested(3)
	print(deep, " ", deep.get_typed_builtin() == TYPE_ARRAY)

	var untyped: Array = singleton(9)
	print(untyped, " ", untyped.get_typed_builtin() == TYPE_NIL)
	print("generic typed container return ok")
