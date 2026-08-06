# A retroactive conformance (`extend Target uses Trait[arg]`) is recorded by trait identity alone, so
# a value that satisfies a trait only through the conformance registry carries no argument evidence.
# A raw trait target still answers from the registry; a specialized one fails rather than accepting
# every specialization, matching the rule a specialized class target follows when the value cannot
# prove its arguments.
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
	print("conformed specialized cast is null: ", conformed_cast == null)
	var conformed_raw_cast: Variant = conformed as GenericStore
	print("conformed raw cast keeps identity: ", conformed_raw_cast == conformed)
	var scripted_cast: Variant = scripted as GenericStore[int]
	print("scripted specialized cast keeps identity: ", scripted_cast == scripted)
