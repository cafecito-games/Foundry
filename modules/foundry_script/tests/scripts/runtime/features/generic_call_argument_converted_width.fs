# A generic callee is compiled once with its method type parameter erased, so its own argument
# binding performs no conversion. The call site is the one frame that knows the substitution, so a
# hard-typed argument the analyzer accepted through an implicit conversion is converted there — the
# same conversion, strict table and all, a concrete parameter of the substituted type performs.
func identity[T](value: T) -> T:
	print(typeof(value), " ", value)
	return value


func pick_tail[T](_head: T, tail: T) -> T:
	print(typeof(tail), " ", tail)
	return tail


func collect[T](...values: Array[T]) -> Array[T]:
	print(values)
	return values


func take(value: int) -> void:
	print(typeof(value), " ", value)


func test() -> void:
	# The generic body observes exactly what the concrete `int` parameter observes: `TYPE_INT`, `7`.
	var f: float = 7.5
	Utils.check(identity[int](f) == 7)
	take(f)

	# The conversion carries the substituted declared width, so `long` keeps a value `int` cannot.
	var neg: float = -2147483649.5
	Utils.check(identity[long](neg) == -2147483649)

	var i: int = 9
	Utils.check(identity[float](i) == 9.0)

	# The rule is the strict-conversion table, not a numeric special case.
	var flag: bool = true
	Utils.check(identity[int](flag) == 1)

	var label: String = "label"
	Utils.check(identity[StringName](label) == &"label")

	# A widening between two integer carriers is accepted because every source value is representable,
	# not because anything converts it; the value passes through unconverted.
	var unsigned_value: uint = 5
	Utils.check(identity[long](unsigned_value) == 5)

	# The explicit application converts every argument slot the substitution closes. The inferred
	# spelling of this call is not equivalent: type parameters are invariant, so a hard `int` head
	# and a hard `float` tail conflict and require the explicit application used here.
	var head: int = 1
	var tail: float = 2.5
	Utils.check(pick_tail[int](head, tail) == 2)

	# A resolved typed-rest element converts element by element.
	var first: float = 1.9
	var second: float = -1.9
	Utils.check(collect[int](first, second) == [1, -1])

	print("generic call argument conversion ok")
