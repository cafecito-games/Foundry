# The known sibling of an open `Self` is real evidence: the recorded `int` contradicts the `float` the
# destination declares, so the Variant-routed store is rejected even though the second position stays
# open. Before the composite survived reification the whole argument was dropped and this store landed.
trait OsdKeeper[T]:
	func label() -> String:
		return "keeper"


class OsdPair[A, B]:
	pass


class OsdDeclared:
	uses OsdKeeper[OsdPair[int, Self]]


func test() -> void:
	var value: Variant = OsdDeclared.new()
	var rejected: OsdKeeper[OsdPair[float, String]] = value
	print(rejected != null)
