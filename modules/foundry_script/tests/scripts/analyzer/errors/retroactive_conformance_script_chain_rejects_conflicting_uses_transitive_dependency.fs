# Loading composes: a file reached through an intermediate dependency is loaded by this one exactly as
# a direct dependency is, and its class `uses` binding constrains the engine chain just the same. The
# intermediate file binds nothing itself, so the contradiction is only visible past it.
const _Bridge = preload("sccut_chain_bridge.notest.fs")


extend RefCounted uses SccutKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
