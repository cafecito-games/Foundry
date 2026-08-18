# A script class's chain continues through the engine class its base ends on, so a conformance on
# that engine ancestry answers for the script class's receivers too. Binding the trait differently on
# the two levels lets a `SccHolder` widened to `RefCounted` be typed against `int` and dispatch the
# `String` witness.
trait SchKeeper[T]:
	abstract func make() -> T


class SchHolder extends RefCounted:
	pass


extend RefCounted uses SchKeeper[int]:
	func make() -> int:
		return 7


extend SchHolder uses SchKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
