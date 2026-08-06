# A retroactive conformance (`extend Target uses Trait[arg]`) records the arguments it declared, so a
# value that satisfies a trait only through the conformance registry answers a specialized target from
# them: an exact match succeeds and a mismatch fails, exactly as it would for a class that declared the
# `uses` clause itself. A raw trait target still asks only the nominal question.
extend Resource uses GenericStore[int]:
	func store(_item: int) -> void:
		pass

	func fetch() -> int:
		return 0


class ScriptedStore:
	uses GenericStore[int]

	var kept: int = 0

	func store(item: int) -> void:
		kept = item

	func fetch() -> int:
		return kept


func test() -> void:
	var conformed: Variant = Resource.new()
	var scripted: Variant = ScriptedStore.new()

	print("resource is GenericStore: ", conformed is GenericStore)
	print("resource is GenericStore[int]: ", conformed is GenericStore[int])
	print("resource is GenericStore[String]: ", conformed is GenericStore[String])

	print("scripted is GenericStore: ", scripted is GenericStore)
	print("scripted is GenericStore[int]: ", scripted is GenericStore[int])
	print("scripted is GenericStore[String]: ", scripted is GenericStore[String])

	var conformed_cast: Variant = conformed as GenericStore[int]
	print("conformed specialized cast keeps identity: ", conformed_cast == conformed)
	var conformed_raw_cast: Variant = conformed as GenericStore
	print("conformed raw cast keeps identity: ", conformed_raw_cast == conformed)
	var conformed_mismatched_cast: Variant = conformed as GenericStore[String]
	print("conformed mismatched cast is null: ", conformed_mismatched_cast == null)
	var scripted_cast: Variant = scripted as GenericStore[int]
	print("scripted specialized cast keeps identity: ", scripted_cast == scripted)
