# A generic function is compiled once with its method type parameter erased, so the callee's own
# argument binding has no run-time type to check an incoming value against. The call site is the only
# place that knows what the call substituted, so a gradual argument is validated and converted there,
# against the substituted type, and the callee body observes the converted value.
class Sample:
	var label: String = "sample"


class Box[T]:
	var value: T


func identity[T](value: T) -> T:
	return value


func pick_tail[T](_head: T, tail: T) -> T:
	return tail


func collect[T](...values: Array[T]) -> Array[T]:
	return values


func forward[U](value: U) -> Variant:
	# `U` is still open here, so the inner call adds no check: this frame has no concrete type to
	# check against either. The outer call already checked what it substituted for `U`.
	return identity[U](value)


func untyped_int() -> Variant:
	return 5


func untyped_float() -> Variant:
	return 7.0


func untyped_text() -> Variant:
	return "text"


func untyped_numbers() -> Variant:
	var numbers: Array[int] = [1, 2]
	return numbers


func untyped_sample() -> Variant:
	return Sample.new()


func untyped_int_box() -> Variant:
	return Box[int].new()


func test() -> void:
	Utils.check(identity[int](untyped_int()) == 5)
	# The value is converted at the call boundary, exactly as a concrete `int` parameter converts it.
	Utils.check(identity[int](untyped_float()) == 7)
	Utils.check(identity[String](untyped_text()) == "text")

	# An inferred substitution behaves identically: `T` is solved from the hard-typed first argument
	# while the tested argument stays gradual.
	var head: int = 1
	Utils.check(pick_tail(head, untyped_float()) == 7)
	Utils.check(pick_tail[int](head, untyped_float()) == 7)

	# A known typed-container substitution goes through the same concrete-container conversion a
	# concrete `Array[int]` parameter performs, and reaches the callee reified rather than erased.
	var numbers: Array[int] = identity[Array[int]](untyped_numbers())
	Utils.check(numbers == [1, 2])
	Utils.check(numbers.is_typed())
	Utils.check(numbers.get_typed_builtin() == TYPE_INT)

	var sample: Sample = identity[Sample](untyped_sample())
	Utils.check(sample.label == "sample")

	# A substituted specialization is decided by its reified type arguments, through the same
	# structural relation every other boundary asks.
	var box: Box[int] = identity[Box[int]](untyped_int_box())
	Utils.check(box != null)

	# A rest element resolved at the call site is checked element by element.
	var collected: Array[int] = collect[int](untyped_int(), untyped_float())
	Utils.check(collected == [5, 7])

	Utils.check(forward[int](untyped_int()) == 5)
	print("generic call argument check ok")
