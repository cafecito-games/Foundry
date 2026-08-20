# A `uses` clause that writes no type arguments still binds a generic trait when the trait it names
# specializes one. The preloaded class applies a non-generic subtrait of "SccgKeeper", which fixes the
# same binding an explicit "SccgKeeper[int]" would, so this engine declaration contradicts it.
const _Holder = preload("sccg_chain_dep_uses.notest.fs")


extend RefCounted uses SccgKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
