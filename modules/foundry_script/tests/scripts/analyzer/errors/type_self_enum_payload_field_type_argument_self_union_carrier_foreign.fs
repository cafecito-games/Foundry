# The generic-class carrier is decomposed like any other, so a specialization matching neither owner's
# alternative is still rejected. The expectation the diagnostic prints is one assembled alternative, not
# a union carrying two members that render alike, and the value side names the specialization actually
# supplied.
class Foreign:
	pass


class Keeper[E]:
	var value: E

	func _init(initial: E) -> void:
		value = initial


class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: Keeper[(int, Self) | (int, T)])

	func construct(other: Receiver, foreign: Foreign) -> void:
		var erased := other.Box[Self].DeclarationFirst(Keeper[Variant].new(0))
		var unrelated := other.Box[Self].DeclarationFirst(Keeper[(int, Foreign)].new((1, foreign)))
		var unrelated_element := other.Box[Self].DeclarationFirst(Keeper[int].new(2))
		print(erased, unrelated, unrelated_element)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new(), Foreign.new())
