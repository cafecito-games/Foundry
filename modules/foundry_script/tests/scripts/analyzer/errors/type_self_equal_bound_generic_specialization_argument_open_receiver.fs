# A class type argument holds `Self` the same way a container element does: a specialization built for
# the calling frame's receiver is not the specialization an open receiver's `Self` resolves to, and a
# specialization is invariant in its argument, so the two are interchangeable only through the calling
# frame's own receiver.
class Box[T]:
	var value: T


class Cell:
	func accept(_held: Box[Self]) -> void:
		pass

	func call_via_base(cell: Cell, held: Box[Self]) -> void:
		cell.accept(held)


func test() -> void:
	pass
