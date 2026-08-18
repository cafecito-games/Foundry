# A visible conformance on the script base contradicts the derived class's own `uses` clause just as a
# binding written on that base would.
const _Ancestor = preload("sau_ancestor_conformance.notest.fs")


class SauHolder extends SauMiddle uses SauKeeper[String]:
	func size() -> int:
		return 1


func test() -> void:
	print("unreachable")
