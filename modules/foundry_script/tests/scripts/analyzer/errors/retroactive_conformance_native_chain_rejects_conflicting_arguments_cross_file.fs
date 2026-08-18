# The conflicting ancestor conformance is declared in a preloaded file, so it is read back from the
# conformance registry rather than from this file's own pending entries.
const _Ancestor = preload("rncc_chain_ancestor_source.notest.fs")


extend RefCounted uses RnccxKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
