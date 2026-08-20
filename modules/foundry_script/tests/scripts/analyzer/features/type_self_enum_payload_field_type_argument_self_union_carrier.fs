# A generic class's type argument is the other carrier a union can legally sit in, and the two owners
# of a `Self` alternative collapse inside it exactly as they do inside a tuple element. Decomposing the
# open schema through the type argument keeps a caller-relative specialization admissible under either
# declaration order.
class Keeper[E]:
	var value: E

	func _init(initial: E) -> void:
		value = initial


class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: Keeper[(int, Self) | (int, T)])
		ArgumentFirst(value: Keeper[(int, T) | (int, Self)])

	func construct(other: Receiver) -> void:
		var declaration_first := Keeper[(int, Self)].new((1, self))
		match other.Box[Self].DeclarationFirst(declaration_first):
			Box[Self].DeclarationFirst(kept):
				prints("declaration-first caller", kept.value[0], kept.value[1] == self)
			_:
				prints("declaration-first caller", "unmatched")
		var argument_first := Keeper[(int, Self)].new((2, self))
		match other.Box[Self].ArgumentFirst(argument_first):
			Box[Self].ArgumentFirst(kept):
				prints("argument-first caller", kept.value[0], kept.value[1] == self)
			_:
				prints("argument-first caller", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
