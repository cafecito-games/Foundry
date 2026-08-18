# A `Self` parameter is a receiver contract only for the direct call its signature was looked up for.
# A signature captured into a `Callable`, or reached reflectively through `call()`, is checked against
# whatever target the callable holds, so no argument at that site can be compared with a receiver
# expression. The three producers of one static callable type must therefore agree.
class Cell:
	var label: String = "?"

	func take(other: Self) -> void:
		print("took ", other.label)

	func route() -> void:
		var cell: Cell = self
		var constructed := Callable(cell, "take")
		constructed.call(self)
		var referenced := cell.take
		referenced.call(self)
		cell.call("take", self)


class Derived extends Cell:
	pass


func test() -> void:
	var derived := Derived.new()
	derived.label = "derived"
	derived.route()
