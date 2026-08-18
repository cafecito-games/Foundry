# The conflicting conformance on the script base is declared in a preloaded file, so it is read back
# from the conformance registry rather than from this file's own pending entries.
const _Ancestor = preload("sax_ancestor_conformance.notest.fs")


class SaxHolder extends SaxMiddle:
	pass


extend SaxHolder uses SaxKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print("unreachable")
