# A dynamic proxy auto-backs every `var` declared in its target's contract:
# get() returns the zero value of the declared type until set() writes a slot.
# Property access is served from the backing store, never the handler.
trait Bag:
	var label: String
	var count: int
	abstract func use() -> void

func test() -> void:
	var handled: Array = []
	var bag: Object = create_proxy_dynamic(Bag, func(method_name: StringName, _args: Array) -> Variant:
		handled.append(method_name)
		return null)

	print(bag.get("count"))
	print(bag.get("label") == "")

	bag.set("count", 7)
	bag.set("label", "hi")
	print(bag.get("count"))
	print(bag.get("label"))

	# Property access did not reach the handler.
	print(handled.is_empty())
