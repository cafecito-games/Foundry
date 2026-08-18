# The same pair with the declarations swapped: the rule compares a conformance against the script
# classes below its target as well as the ones above it, so the contradiction is reported whichever
# declaration is analyzed second.
trait SanrKeeper[T]:
	abstract func make() -> T


class SanrMiddle:
	pass


class SanrHolder extends SanrMiddle:
	pass


extend SanrHolder uses SanrKeeper[String]:
	func make() -> String:
		return "seven"


extend SanrMiddle uses SanrKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
