# A `Type[V]` slot in a generic trait keeps its handle layer through the substitution the implementer
# applies. `HandleKeeper[int]` leaves a slot no class handle can satisfy, so a dynamically produced
# handle is rejected instead of passing through a slot that admits only null.
trait HandleKeeper[V]:
	func keep_handle(value) -> Type[V]:
		var kept: Type[V] = value
		return kept


class IntHandleKeeper:
	uses HandleKeeper[int]


func dynamic_handle():
	return Node


func test() -> void:
	print(IntHandleKeeper.new().keep_handle(dynamic_handle()))
