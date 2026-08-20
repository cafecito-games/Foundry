# Decomposing a union a carrier holds must only ever widen what the position admits. The set itself is
# one of the types the position denotes: a value whose own element type is that set satisfies the slot
# without satisfying any single alternative of it, so each alternative's intact form stays among the
# types asked, under either declaration order.
class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: (String | int, Self) | (String | int, T))
		ArgumentFirst(value: (String | int, T) | (String | int, Self))

	func construct(other: Receiver) -> void:
		var text_element: String | int = "text"
		var declaration_first: (String | int, Self) = (text_element, self)
		match other.Box[Self].DeclarationFirst(declaration_first):
			Box[Self].DeclarationFirst(value):
				prints("declaration-first union element", value[1] == self)
			_:
				prints("declaration-first union element", "unmatched")
		var count_element: String | int = 7
		var argument_first: (String | int, Self) = (count_element, self)
		match other.Box[Self].ArgumentFirst(argument_first):
			Box[Self].ArgumentFirst(value):
				prints("argument-first union element", value[1] == self)
			_:
				prints("argument-first union element", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
