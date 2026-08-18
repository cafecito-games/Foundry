# A script class's chain runs through its script bases before it reaches any engine ancestry, and a
# conformance on a base answers for the derived class's receivers too. Binding the trait differently on
# the two levels lets a `SanHolder` widened to `SanMiddle` be typed against `int` and dispatch the
# `String` witness.
trait SanKeeper[T]:
	abstract func make() -> T


class SanMiddle:
	pass


class SanHolder extends SanMiddle:
	pass


extend SanMiddle uses SanKeeper[int]:
	func make() -> int:
		return 7


extend SanHolder uses SanKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
