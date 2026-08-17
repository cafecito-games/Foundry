# An argument whose static type is an erased container is converted to the declared typed
# container at the call site. That conversion happens after the receiver is evaluated.
# https://github.com/cafecito-games/Foundry/issues/2255

var order: Array = []


class Bag[T]:
	var items: Array[T] = []

	func all() -> Array[T]:
		return items


class Sink:
	func take(values: Array[int]) -> void:
		prints("took", values)


func make_bag() -> Bag[int]:
	order.append("argument")
	var bag := Bag[int].new()
	bag.items.append(1)
	bag.items.append(2)
	return bag


func make_sink() -> Sink:
	order.append("receiver")
	return Sink.new()


func test():
	make_sink().take(make_bag().all())
	prints("eval", order)
