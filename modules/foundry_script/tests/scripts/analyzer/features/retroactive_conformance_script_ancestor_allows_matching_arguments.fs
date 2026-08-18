# Two conformances on one script chain that bind the trait to the same arguments describe the same
# receiver consistently, so both are accepted. A class on a different branch shares no values with
# either, so it may bind the trait however it likes.
trait SamKeeper[T]:
	abstract func make() -> T


class SamMiddle:
	pass


class SamHolder extends SamMiddle:
	pass


class SamSibling:
	pass


extend SamMiddle uses SamKeeper[int]:
	func make() -> int:
		return 7


extend SamHolder uses SamKeeper[int]:
	func make() -> int:
		return 11


extend SamSibling uses SamKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	var holder := SamHolder.new()
	var keeper: SamKeeper[int] = holder
	print(keeper.make())
	var sibling := SamSibling.new()
	var other: SamKeeper[String] = sibling
	print(other.make())
