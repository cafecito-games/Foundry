# The same contradiction with the declarations in the other order. Which one the diagnostic lands on
# depends on declaration order, but the pair is rejected either way.
trait SchrKeeper[T]:
	abstract func make() -> T


class SchrHolder extends RefCounted:
	pass


extend SchrHolder uses SchrKeeper[String]:
	func make() -> String:
		return "seven"


extend RefCounted uses SchrKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
