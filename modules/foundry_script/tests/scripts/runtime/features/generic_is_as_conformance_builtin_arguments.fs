# A retroactively conformed builtin answers a specialized trait target from the arguments the
# conformance declared, including when the value travels through a `Variant`-typed local. A
# statically typed test against a contradicted argument is decided by the analyzer instead and lives
# in analyzer/errors/retroactive_conformance_test_rejects_conflicting_evidence.fs.
extend int uses GenericStore[int]:
	func store(_item: int) -> void:
		pass

	func fetch() -> int:
		return 0


func test() -> void:
	var n := 3
	print("int is GenericStore: ", n is GenericStore)
	print("int is GenericStore[int]: ", n is GenericStore[int])

	var v: Variant = n
	print("variant is GenericStore: ", v is GenericStore)
	print("variant is GenericStore[int]: ", v is GenericStore[int])
	print("variant is GenericStore[String]: ", v is GenericStore[String])

	var raw_cast: Variant = v as GenericStore
	print("raw cast keeps identity: ", raw_cast == v)
	var exact_cast: Variant = v as GenericStore[int]
	print("exact cast keeps identity: ", exact_cast == v)
	var mismatched_cast: Variant = v as GenericStore[String]
	print("mismatched cast is null: ", mismatched_cast == null)
