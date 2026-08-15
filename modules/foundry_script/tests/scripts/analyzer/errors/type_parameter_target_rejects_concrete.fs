# A type parameter has no runtime existence of its own: the caller picks `X` and it is erased before
# the callee runs, so no emitted check can decide whether a value "is an X". Only a value already
# known to be `X` satisfies an `X` slot. Flow narrowing refines the value, never the parameter, so a
# value narrowed to `int` is an `int` from then on and no longer satisfies a `-> X` return.
func passthrough[X: Number](value: X) -> X:
	if value is int:
		return value
	return value


func seeded[X: Number](value: X) -> X:
	var slot: X = 5
	print(value)
	return slot


func test():
	print(passthrough(7))
	print(seeded(9))
