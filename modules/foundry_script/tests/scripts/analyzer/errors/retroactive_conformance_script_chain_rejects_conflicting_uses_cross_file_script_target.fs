# The script-class half of the same rule: the conformance target is a script class, and the binding
# it contradicts is a preloaded file's descendant class applying the trait through its own `uses`.
const _Holder = preload("sccus_chain_dep_uses.notest.fs")


extend SccusBase uses SccusKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
