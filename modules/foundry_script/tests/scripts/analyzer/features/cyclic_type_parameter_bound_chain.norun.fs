# Mutually dependent bounds have no concrete link for member resolution to reach, and are rejected
# where the bound itself is resolved, before any member access is analyzed. Following bound chains
# during member resolution must not change that: the declaration is still reported, not walked.
class Ring[T: U, U: T]:
	var value: T

	func poke() -> void:
		value.anything()
