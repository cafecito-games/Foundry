# An inferred substitution is checked exactly like an explicit one. `T` is solved from the hard-typed
# first argument, and the gradual second argument is validated against that solution at the call site.
func pick_tail[T](_head: T, tail: T) -> T:
	print("callee entered")
	return tail


func untyped_text() -> Variant:
	return "not an int"


func test() -> void:
	var head: int = 1
	print(pick_tail(head, untyped_text()))
