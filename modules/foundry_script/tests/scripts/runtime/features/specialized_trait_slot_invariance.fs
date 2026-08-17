# A specialized trait slot is the trait half of the same rule: `Keeper[int]` and `Keeper[String]` are
# the same trait nominally and different types here, so the arguments an implementer conformed with are
# compared at every boundary the structural predicate answers.
#
# Both sources of conformance evidence take part: the binding table a declared `uses Keeper[args]`
# clause fills, and the arguments a retroactive `extend ... uses Keeper[args]` recorded. As for a class
# slot, `is` narrows and the stores stay gradual.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class IntKeeper:
	uses Keeper[int]


class StringKeeper:
	uses Keeper[String]


class ForwardingKeeper[U]:
	uses Keeper[U]


class RetroTarget:
	pass


extend RetroTarget uses Keeper[int]:
	func label() -> String:
		return "retro"


class TraitHolder:
	var kept: Keeper[int]


func supply(value: Variant) -> Variant:
	return value


func report_tests(label: String, value: Variant) -> void:
	print(label, ": top-level is=", value is Keeper[int],
			", nested is=", supply((1, value)) is (int, Keeper[int]))


func stores_member(value: Variant) -> bool:
	var holder: Object = TraitHolder.new()
	holder.set("kept", value)
	return holder.get("kept") == value


func test() -> void:
	var right := IntKeeper.new()
	var wrong := StringKeeper.new()
	var forwarded := ForwardingKeeper[int].new()
	var raw := ForwardingKeeper.new()
	var retro := RetroTarget.new()

	report_tests("declared Keeper[int]", right)
	report_tests("declared Keeper[String]", wrong)
	report_tests("forwarded Keeper[int]", forwarded)
	report_tests("unspecialized forwarder", raw)
	report_tests("retroactively conformed", retro)

	print("trait member store: right=", stores_member(right),
			" wrong=", stores_member(wrong),
			" forwarded=", stores_member(forwarded),
			" raw=", stores_member(raw),
			" retro=", stores_member(retro))

	print("specialized trait slot invariance ok")
