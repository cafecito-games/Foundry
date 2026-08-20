# The reversed direction of the direct-uses cross-file rule: the binding lives in a preloaded file's
# class `uses` clause, and the engine declaration analyzed here is what has to be rejected. Only this
# file can see both sides, so this is the only place the contradiction can be reported.
const _Holder = preload("sccur_chain_dep_uses.notest.fs")


extend RefCounted uses SccurKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
