# The same contradiction with the declarations in the other order. Which one the diagnostic lands on
# depends on declaration order, but the pair is rejected either way.
trait RnccrKeeper[T]:
	abstract func make() -> T


extend RefCounted uses RnccrKeeper[String]:
	func make() -> String:
		return "seven"


extend Object uses RnccrKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
