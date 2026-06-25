# A dynamic proxy auto-backs declared `var`s, but writes must validate/coerce
# against the declared type exactly as a normal GDScript instance does, so the
# proxy cannot violate the proxied type's property contract.
trait Bag:
	var count: int
	var tags: Array[int]
	@abstract func use() -> void

func test() -> void:
	var bag: Object = create_proxy_dynamic(Bag, func(_method: StringName, _args: Array) -> Variant:
		return null)

	# A non-numeric string coerces to int 0 (as for a real `int` member), not stored
	# verbatim.
	bag.set("count", "oops")
	print(bag.get("count"))

	# A float coerces to int.
	bag.set("count", 4.0)
	print(bag.get("count"))

	# An untyped Array is rejected for an Array[int] member; the slot is unchanged.
	bag.set("tags", ["x"])
	print(bag.get("tags"))

	# A correctly-typed Array[int] is accepted.
	var typed: Array[int] = [1, 2]
	bag.set("tags", typed)
	print(bag.get("tags"))
