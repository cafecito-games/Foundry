# The other cross-file direction: the script-class conformance is the preloaded one, and the engine
# declaration analyzed here is what the rule has to reject. The pair is rejected whichever file is
# analyzed second.
const _Holder = preload("sccx_chain_script_source.notest.fs")


extend RefCounted uses SccxKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
