# An `else` branch that returns is an unreachable join path, so the final is
# still definitely assigned afterwards.
class Guarded:
	final var id: int

	func _init(valid: bool) -> void:
		if valid:
			id = 1
		else:
			return
		print("id is %d" % id)

func test() -> void:
	Guarded.new(true)
