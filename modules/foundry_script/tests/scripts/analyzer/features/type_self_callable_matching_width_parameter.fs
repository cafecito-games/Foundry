# The matching width is admitted and dispatches: a callable's fixed-width parameter is identical on
# both sides, so the `Self` tail is all that remains to reconcile.
class Cell:
	func fan(callback: Callable[[long, ...Array[Self]], void]) -> void:
		callback.call(9223372036854775807, self)

	func sink(value: long, ...values: Array) -> void:
		print("sink ", value, " ", values.size())

	func run() -> void:
		var wide: Callable[[long, ...Array], void] = sink
		fan(wide)


func test() -> void:
	Cell.new().run()
