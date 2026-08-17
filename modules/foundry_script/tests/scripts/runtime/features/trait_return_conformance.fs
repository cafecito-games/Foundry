# A trait-typed return answers membership from conformance evidence, not from a base-script chain: a
# trait never appears in a class inheritance chain, so a declared `uses`, an inherited one, a
# supertrait, or a retroactive `extend ... uses` is the only thing that can prove it. A specialized
# trait return then asks the gradual argument question -- absent evidence is accepted, and only
# evidence that contradicts the declaration is rejected.
#
# Every return here goes through a `Variant` source, which is what forces the statically untyped
# return path rather than a compile-time answer.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class IntKeeper:
	uses Keeper[int]


class DerivedIntKeeper extends IntKeeper:
	pass


class StoringKeeper:
	uses Storing[int]


class ForwardingKeeper[U]:
	uses Keeper[U]


class RetroTarget:
	pass


extend RetroTarget uses Keeper[int]:
	func label() -> String:
		return "retroactive script"


extend Resource uses Keeper[int]:
	func label() -> String:
		return "retroactive native"


extend int uses Keeper[int]:
	func label() -> String:
		return "retroactive builtin"


class Source:
	func give(value: Variant) -> Keeper[int]:
		return value

	func give_raw(value: Variant) -> Keeper:
		return value


func test() -> void:
	var source := Source.new()

	print("declared: ", source.give(IntKeeper.new()) != null)
	print("subclass: ", source.give(DerivedIntKeeper.new()) != null)
	print("supertrait: ", source.give(StoringKeeper.new()) != null)
	print("forwarded: ", source.give(ForwardingKeeper[int].new()) != null)
	print("absent evidence: ", source.give(ForwardingKeeper.new()) != null)
	print("retroactive script: ", source.give(RetroTarget.new()) != null)
	print("retroactive native: ", source.give(Resource.new()) != null)
	print("retroactive builtin: ", source.give(7) != null)
	print("null: ", source.give(null) == null)
	print("unspecialized trait return: ", source.give_raw(IntKeeper.new()) != null)
