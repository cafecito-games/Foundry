# A signature captured into a `Callable` is checked later against whatever target the callable holds,
# so its `Self` positions are not a receiver contract at any depth: a carrier reaching the parameter
# through a container element, a class type argument, or a nested callable signature is admitted the
# same way, whichever receiver the capture named.
class Box[T]:
	var value: T


class Cell:
	func accept(held: Box[Self]) -> void:
		print("boxed ", held.value != null)

	func take_items(items: Array[Self]) -> void:
		print("items ", items.size())

	func accept_callback(callback: Callable[[Self], void]) -> void:
		callback.call(self)

	func greet(_other: Self) -> void:
		print("greeted")

	func run_via(other: Cell) -> void:
		var held := Box[Self].new()
		held.value = self
		var items: Array[Self] = [self]
		var callback: Callable[[Self], void] = greet
		Callable(other, "accept").call(held)
		Callable(other, "take_items").call(items)
		Callable(other, "accept_callback").call(callback)


func test() -> void:
	Cell.new().run_via(Cell.new())
