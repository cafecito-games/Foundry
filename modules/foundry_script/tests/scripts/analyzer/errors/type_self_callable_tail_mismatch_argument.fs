# The `Self` contract decides a callable argument by structural identity, so a tail the parameter
# declares is part of what the argument must supply: a tail whose element type differs is rejected, and
# so is a fixed-arity callable where the parameter declares a tail it will invoke. The contravariant
# directions are admitted instead -- a tail the parameter never invokes, and a gradual tail that
# accepts whatever the parameter sends -- but the `Self` those signatures carry still answers to
# receiver identity, so each relaxation reaches only the calling frame's own receiver.
class Cell:
	func self_tail(_callback: Callable[[...Array[Self]], void]) -> void:
		pass

	func self_tail_declared(_callback: Callable[[Self, ...Array], void]) -> void:
		pass

	func self_fixed(_callback: Callable[[Self], void]) -> void:
		pass

	func route(other: Cell, wrong: Callable[[...Array[String]], void], fixed: Callable[[Self], void]) -> void:
		other.self_tail(wrong)
		self_tail(wrong)
		other.self_tail_declared(fixed)
		self_tail_declared(fixed)

	func route_relaxed(other: Cell, variadic: Callable[[Self, ...Array], void]) -> void:
		other.self_fixed(variadic)
		self_fixed(variadic)

	func route_gradual(other: Cell, gradual: Callable[[...Array], void]) -> void:
		other.self_tail(gradual)
		self_tail(gradual)


func test() -> void:
	pass
