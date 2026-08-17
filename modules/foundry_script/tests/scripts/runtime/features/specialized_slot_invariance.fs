# A script-typed slot that declares type arguments enforces them, because the declared specialization
# is part of the type: a nominally correct value carrying somebody else's arguments is not a value of
# it. The structural predicate answers for every boundary at once -- the tuple element test, the
# dynamic member and static member stores, the dynamic call boundary and the typed return -- so the
# five cannot drift apart the way the nested and top-level `is` spellings once did.
#
# The two rules differ on purpose. A store is gradual: only evidence that contradicts the declared
# arguments rejects, so a raw, unspecialized instance is written the way every other store in the
# language accepts it. An `is` test narrows, so it demands positive, complete evidence and answers
# `false` for that same raw instance. The store is therefore always the laxer of the two, never the
# stricter.
class Pair[A, B]:
	var first: A
	var second: B


class StringPair extends Pair[int, String]:
	pass


class Holder:
	var pair: Pair[int, String]
	static var shared: Pair[int, String]

	func take(value: Pair[int, String]) -> String:
		return "took " + str(value != null)

	func give(value: Variant) -> Pair[int, String]:
		return value


class Crate[T]:
	func keep(value) -> (int, T):
		var kept: (int, T) = value
		return kept


func supply(value: Variant) -> Variant:
	return value


# The two spellings of the same question. They must agree for every value.
func report_tests(label: String, value: Variant) -> void:
	print(label, ": top-level is=", value is Pair[int, String],
			", nested is=", supply((1, value)) is (int, Pair[int, String]))


# `Object.set()` reports a rejected write by leaving the member untouched, which makes the store
# boundary observable without ending the program the way a typed assignment statement would.
func stores_member(value: Variant) -> bool:
	var holder: Object = Holder.new()
	holder.set("pair", value)
	return holder.get("pair") == value


func stores_static_member(value: Variant) -> bool:
	var holder: Object = Holder.new()
	holder.set("shared", null)
	holder.set("shared", value)
	var kept: bool = holder.get("shared") == value
	holder.set("shared", null)
	return kept


func test() -> void:
	var good := Pair[int, String].new()
	var wrong := Pair[int, Node].new()
	var raw := Pair.new()
	var subclass := StringPair.new()

	report_tests("correctly specialized", good)
	report_tests("wrongly specialized", wrong)
	report_tests("unspecialized", raw)
	report_tests("subclass fixing the arguments", subclass)

	print("member store: good=", stores_member(good),
			" wrong=", stores_member(wrong),
			" raw=", stores_member(raw),
			" subclass=", stores_member(subclass))

	print("static member store: good=", stores_static_member(good),
			" wrong=", stores_static_member(wrong),
			" raw=", stores_static_member(raw),
			" subclass=", stores_static_member(subclass))

	# The gradual rule at the remaining boundaries: everything but the wrongly specialized value is
	# accepted, including the raw instance the narrowing `is` above answered `false` for.
	var holder := Holder.new()
	var take: Callable = Callable(holder, "take")
	print("call boundary: good=", take.call(good),
			" raw=", take.call(raw),
			" subclass=", take.call(subclass))

	print("typed return: good=", holder.give(supply(good)) != null,
			" raw=", holder.give(supply(raw)) != null,
			" subclass=", holder.give(supply(subclass)) != null)

	var crate := Crate[Pair[int, String]].new()
	print("tuple element store: good=", crate.keep(supply((1, good))) != null,
			" raw=", crate.keep(supply((2, raw))) != null,
			" subclass=", crate.keep(supply((3, subclass))) != null)

	print("specialized slot invariance ok")
