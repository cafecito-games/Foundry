# The `Self` contract decides a callable argument by structural identity, so the variadic tail is part
# of that structure: a tail whose element type differs from the parameter's is rejected through any
# receiver, exactly as a mismatched fixed parameter is. Variadicity itself counts too, because a
# gradual tail records only the arity bit and no element type.
class Cell:
	func self_tail(_callback: Callable[[...Array[Self]], void]) -> void:
		pass

	func self_fixed(_callback: Callable[[Self], void]) -> void:
		pass

	func route(other: Cell, wrong: Callable[[...Array[String]], void], extra: Callable[[Self, ...Array], void]) -> void:
		other.self_tail(wrong)
		self_tail(wrong)
		other.self_fixed(extra)
		self_fixed(extra)


func test() -> void:
	pass
