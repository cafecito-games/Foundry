# An explicit type-argument list does not change which receiver a call dispatches on: `name[T](...)`
# and `self.name[T](...)` run against the calling frame's own receiver, and `receiver.name[T](...)`
# against that receiver. The `Self` receiver contract must read the same answer through the bracket
# spelling as it does through the bracket-free one.
class Cell:
	var label: String = "?"

	func pair[T](first: T, second: Self) -> void:
		print("pair ", first, " ", second.label)

	func route(other: Cell) -> void:
		pair[int](1, self)
		self.pair[int](2, self)
		other.pair[int](3, other)


class Derived extends Cell:
	pass


func test() -> void:
	var derived := Derived.new()
	derived.label = "derived"
	var plain := Cell.new()
	plain.label = "plain"
	derived.route(plain)
