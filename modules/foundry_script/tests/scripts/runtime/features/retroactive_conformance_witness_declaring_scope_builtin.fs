# The declaration-site fallback is one language rule, not a native-target exception: a builtin
# value-type target's stand-in has no lexical outer either, so its witness reaches this file's own
# generic through the same fallback, in the return annotation and in the body.
#
# Inside the witness `Self` is the conformance target, not the script that happens to own the
# compiled witness, so `Crate[Self]` reifies `T` to the builtin target at run time. Both validation
# consumers are exercised: `store()` assigns a `T`-typed parameter from inside the generic, and the
# witness writes the member directly from outside it. A direct `Crate[Self].new()` and a stored
# `Crate[Self]` class handle must reify the same argument.
class Crate[T]:
	var value: T

	func store(next: T) -> void:
		value = next

	func summary() -> String:
		return "crate:" + str(value)


trait Cratable:
	abstract static func packed(value: Self) -> Crate[Self]
	abstract static func aliased(value: Self) -> Crate[Self]


extend int uses Cratable:
	static func packed(value: Self) -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		made.store(value)
		return made

	static func aliased(value: Self) -> Crate[Self]:
		var handle := Crate[Self]
		var made: Crate[Self] = handle.new()
		made.value = value
		return made


# A Dictionary target treats any unresolved name as a potential key, so the fallback has to be
# consulted ahead of that catch-all or the helper type would silently widen to Variant. It also
# proves the rule is about the conformance target rather than about `int`.
extend Dictionary uses Cratable:
	static func packed(value: Self) -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		made.store(value)
		return made

	static func aliased(value: Self) -> Crate[Self]:
		var handle := Crate[Self]
		var made: Crate[Self] = handle.new()
		made.value = value
		return made


func test() -> void:
	print(int.packed(7).summary())
	print(int.aliased(9).summary())
	print(Dictionary.packed({"key": 1}).summary())
	print(Dictionary.aliased({"other": 2}).summary())
