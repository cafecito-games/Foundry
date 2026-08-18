# The conflicting engine-ancestor conformance is declared in a preloaded file, so it is read back from
# the conformance registry rather than from this file's own pending entries.
const _Ancestor = preload("scc_chain_native_source.notest.fs")


class SccHolder extends RefCounted:
	pass


extend SccHolder uses SccKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
