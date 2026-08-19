# The implicit widening across the integer carriers targets `long` only: a `uint` argument does not
# bind to an `int`-width parameter, statically or at runtime.
func take(value: int) -> void:
	print(value)


func test() -> void:
	var u: uint = 5
	take(u)
