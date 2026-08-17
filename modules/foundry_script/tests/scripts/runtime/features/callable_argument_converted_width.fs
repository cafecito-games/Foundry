# A dynamic call converts an argument that is not already a value of the parameter's type, and the
# declared width is asked again about the converted value. Truncation toward zero is unchanged, so
# every result below still fits the parameter it lands in and the call goes through. A wider
# parameter accepts the same magnitude a narrower one would reject, which is what makes the check
# the declaration talking rather than a blanket ceiling.
func take_int(value: int) -> void:
	print(value)


func take_long(value: long) -> void:
	print(value)


func take_unsigned(value: uint) -> void:
	print(value)


func take_rest(...values: Array[int]) -> void:
	print(values)


func test() -> void:
	var narrow: Callable = take_int
	narrow.call(2147483647.0)
	narrow.call(-2147483648.0)
	narrow.call(1.9)
	narrow.call(-1.9)
	narrow.call(0.5)
	narrow.call(-0.5)
	narrow.call(true)

	var wide: Callable = take_long
	wide.call(2147483648.0)
	wide.call(-2147483649.0)

	# The unsigned carrier has no implicit source, so the maximum arrives on its own carrier and the
	# width check accepts it unchanged.
	var unsigned: Callable = take_unsigned
	unsigned.call(4294967295U)

	# A typed rest parameter converts each element through the same boundary.
	var rest: Callable = take_rest
	rest.call(1.9, -1.9, 2147483647.0)
