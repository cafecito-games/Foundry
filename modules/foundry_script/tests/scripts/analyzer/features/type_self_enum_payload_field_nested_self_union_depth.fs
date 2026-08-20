# The decomposition follows every carrier the declaration's open schema puts between the payload field
# and the union, not just the first one, so a union two tuple levels down keeps both of its owners.
class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: (((int, Self) | (int, T), int), int))
		ArgumentFirst(value: (((int, T) | (int, Self), int), int))

	func construct(other: Receiver) -> void:
		match other.Box[Self].DeclarationFirst((((1, self), 10), 100)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first caller", value[0][0][0], value[0][0][1] == self, value[0][1], value[1])
			_:
				prints("declaration-first caller", "unmatched")
		match other.Box[Self].DeclarationFirst((((2, other), 20), 200)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first receiver", value[0][0][0], value[0][0][1] == other, value[0][1], value[1])
			_:
				prints("declaration-first receiver", "unmatched")
		match other.Box[Self].ArgumentFirst((((3, self), 30), 300)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first caller", value[0][0][0], value[0][0][1] == self, value[0][1], value[1])
			_:
				prints("argument-first caller", "unmatched")
		match other.Box[Self].ArgumentFirst((((4, other), 40), 400)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first receiver", value[0][0][0], value[0][0][1] == other, value[0][1], value[1])
			_:
				prints("argument-first receiver", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
