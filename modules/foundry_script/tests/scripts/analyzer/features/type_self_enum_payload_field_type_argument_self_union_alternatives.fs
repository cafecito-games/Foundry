# Two alternatives of a union payload field can be written over different owners and still describe
# the same type once the application binds `T := Self`. Which frame a `Self` belongs to is provenance
# rather than type identity, so union normalization keeps only one of the two; each alternative is
# therefore asked off the declaration's open schema, where both owners still exist. Both owners are
# admissible under either declaration order.
class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: (int, Self) | (int, T))
		ArgumentFirst(value: (int, T) | (int, Self))

	func construct(other: Receiver) -> void:
		match other.Box[Self].DeclarationFirst((1, self)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first caller", value[0], value[1] == self)
			_:
				prints("declaration-first caller", "unmatched")
		match other.Box[Self].DeclarationFirst((2, other)):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first receiver", value[0], value[1] == other)
			_:
				prints("declaration-first receiver", "unmatched")
		match other.Box[Self].ArgumentFirst((3, self)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first caller", value[0], value[1] == self)
			_:
				prints("argument-first caller", "unmatched")
		match other.Box[Self].ArgumentFirst((4, other)):
			Box[Self].ArgumentFirst(value):
				prints("argument-first receiver", value[0], value[1] == other)
			_:
				prints("argument-first receiver", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
