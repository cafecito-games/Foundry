# A generic class's type argument is the other carrier the open schema is walked through, so a union
# reached only by descending into a specialization and then into that specialization's tuple keeps both
# of its owners. The caller-relative alternative stays admissible under either declaration order.
class Keeper[E]:
	var value: E

	func _init(initial: E) -> void:
		value = initial


class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: Keeper[((int, Self) | (int, T), int)])
		ArgumentFirst(value: Keeper[((int, T) | (int, Self), int)])

	func construct(other: Receiver) -> void:
		var declaration_first := Keeper[((int, Self), int)].new(((1, self), 10))
		match other.Box[Self].DeclarationFirst(declaration_first):
			Box[Self].DeclarationFirst(kept):
				prints("declaration-first caller", kept.value[0][0], kept.value[0][1] == self, kept.value[1])
			_:
				prints("declaration-first caller", "unmatched")
		var argument_first := Keeper[((int, Self), int)].new(((2, self), 20))
		match other.Box[Self].ArgumentFirst(argument_first):
			Box[Self].ArgumentFirst(kept):
				prints("argument-first caller", kept.value[0][0], kept.value[0][1] == self, kept.value[1])
			_:
				prints("argument-first caller", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
