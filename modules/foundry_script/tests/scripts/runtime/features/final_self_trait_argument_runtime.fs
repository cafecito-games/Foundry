# The runtime record of a reified `Self` has to answer exactly what the analyzer decided, so every row
# routes through a `Variant` to keep the analyzer out of the way. A `final` non-generic implementer
# tests `true` at its own class and rejects a different argument; a non-final implementer, a subclass
# of one, and a `final` generic implementer keep `Self` open, so they carry no evidence and a store
# accepts while a test rejects. Each case prints the store answer and the test answer as a pair.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class Pair[A, B]:
	pass


final class Closed:
	uses Keeper[Pair[int, Self]]


final class SupertraitClosed:
	uses Storing[Pair[int, Self]]


class OpenSelf:
	uses Keeper[Pair[int, Self]]


class OpenSelfChild extends OpenSelf:
	pass


final class FinalBox[V]:
	uses Keeper[Pair[int, Self]]


func test() -> void:
	var closed: Variant = Closed.new()
	var closed_slot: Keeper[Pair[int, Closed]] = closed
	print("closed store: ", closed_slot != null)
	print("closed test: ", closed is Keeper[Pair[int, Closed]])
	print("closed mismatch test: ", closed is Keeper[Pair[int, String]])

	var supertrait: Variant = SupertraitClosed.new()
	print("supertrait test: ", supertrait is Keeper[Pair[int, SupertraitClosed]])
	print("supertrait mismatch test: ", supertrait is Keeper[Pair[int, String]])

	var open_self: Variant = OpenSelf.new()
	var open_self_slot: Keeper[Pair[int, String]] = open_self
	print("open store: ", open_self_slot != null)
	print("open test: ", open_self is Keeper[Pair[int, String]])

	var open_child: Variant = OpenSelfChild.new()
	print("child test: ", open_child is Keeper[Pair[int, String]])

	var final_generic: Variant = FinalBox[int].new()
	print("final generic test: ", final_generic is Keeper[Pair[int, String]])
