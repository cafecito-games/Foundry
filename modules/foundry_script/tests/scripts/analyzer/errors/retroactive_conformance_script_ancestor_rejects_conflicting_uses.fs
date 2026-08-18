# A derived class's own `uses` clause fixes the trait's arguments the same way a conformance does, so a
# conformance on the script base it extends contradicts it.
trait SanuKeeper[T]:
	abstract func make() -> T


class SanuMiddle:
	pass


class SanuHolder extends SanuMiddle uses SanuKeeper[String]:
	func make() -> String:
		return "seven"


extend SanuMiddle uses SanuKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
