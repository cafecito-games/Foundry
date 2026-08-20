# The two ways a binding can hide from a declaration, combined: the binding class is reached only
# through an intermediate dependency, and it writes no type arguments of its own.
const _Bridge = preload("sccg_chain_bridge.notest.fs")


extend RefCounted uses SccgKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
