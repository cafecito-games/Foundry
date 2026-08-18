# The retroactive half of the same rule. A conformance record stores the known shell and children
# around an unconstrained `Self` node, and the recorded `int` rejects a `float` destination.
trait OsrKeeper[T]:
	func label() -> String:
		return "keeper"


class OsrPair[A, B]:
	pass


class OsrTarget:
	pass


extend OsrTarget uses OsrKeeper[OsrPair[int, Self]]:
	pass


func test() -> void:
	var value: Variant = OsrTarget.new()
	var rejected: OsrKeeper[OsrPair[float, String]] = value
	print(rejected != null)
