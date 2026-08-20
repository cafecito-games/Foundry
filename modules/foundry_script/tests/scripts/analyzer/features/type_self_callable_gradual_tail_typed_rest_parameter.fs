# A gradual variadic tail (`...Array`) accepts every trailing argument, so it satisfies a parameter
# whose tail is typed with `Self`: whatever the callee sends, the callback takes. Through the calling
# frame's own receiver the contract admits it and the call dispatches with the frame's own instances;
# a foreign receiver still answers the tail's `Self` by identity, pinned in
# `analyzer/errors/type_self_callable_tail_mismatch_argument.fs`.
class Cell:
	func fan(callback: Callable[[...Array[Self]], void]) -> void:
		callback.call(self, self)

	func sink(...values: Array) -> void:
		print("sink ", values.size())

	func run() -> void:
		var gradual: Callable[[...Array], void] = sink
		fan(gradual)


func test() -> void:
	Cell.new().run()
