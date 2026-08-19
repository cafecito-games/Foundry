# A statically accepted `uint` -> `long` widening also crosses at every runtime argument binding:
# the value is re-carriered onto `Variant::INT` by the same value-checked design-6.1 rule the typed
# assignment and return opcodes already apply, so a direct call, a defaulted call, a typed lambda, a
# signal slot, a dynamic call, a rest-parameter element, and an erased generic boundary all observe
# exactly what a concrete `long` parameter observes.
signal ping(value: long)


func take(value: long) -> void:
	print(value)


func take_with_default(value: long, extra: long = 1) -> void:
	print(value + extra)


func take_rest(...values: Array[long]) -> void:
	print(values)


func identity[T](value: T) -> T:
	var observed: Variant = value
	print(observed is long, " ", observed is ulong)
	return value


func observe[T](value: T) -> T:
	print(typeof(value))
	return value


func test() -> void:
	var u: uint = 5
	take(u)
	take_with_default(u)

	var typed_lambda: Callable = func(value: long) -> void:
		print(value)
	typed_lambda.call(u)

	var connected := ping.connect(take)
	print(connected == OK)
	ping.emit(u)

	self.call("take", u)

	take_rest(u, u)

	# The generic body holds the substituted carrier, not the argument's source carrier, and the
	# converted carrier survives the caller's typed store of the result.
	var g: long = identity[long](u)
	print(g is long)

	# The same rule for the rest of the implicit-conversion class: the erased body observes the
	# substituted type's carrier (`TYPE_FLOAT` is 3, `TYPE_NODE_PATH` is 22).
	var i: int = 9
	var _from_int: float = observe[float](i)
	var _from_uint: float = observe[float](u)
	var path_text: String = "a/b"
	var _path: NodePath = observe[NodePath](path_text)

	print("uint to long call boundaries ok")
