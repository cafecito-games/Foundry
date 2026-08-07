# Mutually dependent bounds have no concrete link to resolve against. Following the chain must
# terminate on the cycle instead of looping forever, leaving the receiver typed as the parameter it
# started as, which keeps member access on the soft path.
class Ring[T: U, U: T]:
	var value: T

	func poke() -> void:
		value.anything()
