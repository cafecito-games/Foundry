# Two alternatives of a union payload field that differ only in which frame their `Self` belongs to
# collapse under substitution just as readily one carrier deep as they do at the field's own type. The
# declaration's open schema is therefore decomposed through the carrier -- here an unnamed tuple's
# element -- so both owners stay admissible under either declaration order.
class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: ((int, Self) | (int, T), int))
		ArgumentFirst(value: ((int, T) | (int, Self), int))

	func construct(other: Receiver) -> void:
		match other.Box[Self].DeclarationFirst(((1, self), 10)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first caller", value[0][0], value[0][1] == self, value[1])
			_:
				prints("declaration-first caller", "unmatched")
		match other.Box[Self].DeclarationFirst(((2, other), 20)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first receiver", value[0][0], value[0][1] == other, value[1])
			_:
				prints("declaration-first receiver", "unmatched")
		match other.Box[Self].ArgumentFirst(((3, self), 30)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first caller", value[0][0], value[0][1] == self, value[1])
			_:
				prints("argument-first caller", "unmatched")
		match other.Box[Self].ArgumentFirst(((4, other), 40)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first receiver", value[0][0], value[0][1] == other, value[1])
			_:
				prints("argument-first receiver", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
