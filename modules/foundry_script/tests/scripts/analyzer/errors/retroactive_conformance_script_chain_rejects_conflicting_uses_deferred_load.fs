# The load edge is a `preload` inside a function body, so the conformance it brings in is not
# registered while this file's `uses` clauses are checked. Publishing this class's binding is where the
# contradiction is decided, against the registry as it stands at that write, so the verdict does not
# depend on when the dependency happened to be registered.
class SccdHolder extends RefCounted uses SccdKeeper[String]:
	func make() -> String:
		return "seven"


func test() -> void:
	print(preload("sccd_chain_extend.notest.fs") != null)
