# A statically typed supertype argument is accepted here only on the promise of a run-time check --
# that is what the unsafe-call-argument warning means. The erased destination is the one place that
# promise was never kept, so the narrowing is decided at the call site instead.
func identity[T](value: T) -> T:
	print("callee entered")
	return value


func test() -> void:
	var holder: Object = Object.new()
	var narrowed: Resource = identity[Resource](holder)
	print(narrowed)
