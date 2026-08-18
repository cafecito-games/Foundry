# A class's own `uses` clause fixes the trait's arguments the same way a conformance does, so a
# visible conformance on the class's engine ancestry contradicts it just as an ancestor class would.
const _Ancestor = preload("sccu_chain_native_source.notest.fs")


class SccuHolder extends RefCounted uses SccuKeeper[String]:
	func size() -> int:
		return 1


func test() -> void:
	print("unreachable")
