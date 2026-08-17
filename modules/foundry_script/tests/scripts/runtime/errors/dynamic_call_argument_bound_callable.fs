# A bound argument is checked by the callee like any other, and the reported position is the one the
# callee counted: the bound value sits after the arguments the call site passed.
func take(first: int, second: String) -> void:
	print("took ", first, " ", second)


func test() -> void:
	var callback: Callable = take
	var bound := callback.bind(7)
	bound.call(1)
	print("unreachable")
