# `Closed` is final and non-generic, so the `Self` it wrote inside `Pair[int, Self]` denotes `Closed`
# itself. The destination declares `Pair[int, String]` at the same position, which the implementer's
# own binding now contradicts, so the store is rejected at analysis time instead of landing a
# `Keeper[Pair[int, Closed]]` in it.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Pair[A, B]:
	pass


final class Closed:
	uses Keeper[Pair[int, Self]]


func test() -> void:
	var slot: Keeper[Pair[int, String]] = Closed.new()
	print(slot.label())
