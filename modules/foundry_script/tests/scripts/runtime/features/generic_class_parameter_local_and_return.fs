# A class type parameter is reified onto the instance, so a function-body slot declared with it --
# a local, a later store into that local, or the return -- is validated against the argument this
# receiver actually carries, without generating a separate method body per specialization. The check
# happens at the slot's own boundary, so it holds no matter what the caller consumes the result as.
# The slot itself stays erased: the check rejects values the receiver cannot hold, it does not retype
# the value a gradual consumer receives.
class Crate[T]:
	func keep(value) -> T:
		var kept: T = value
		return kept

	func replace(first, second) -> T:
		var kept: T = first
		kept = second
		return kept

	func collect(value) -> Array[T]:
		var kept: Array[T] = [value]
		return kept

	func keep_optional(value) -> T?:
		# A nullable slot admits null, which no container type can describe, so this one stays gradual
		# rather than rejecting the nulls it legitimately holds. Same deliberate limitation a nullable
		# member binding has.
		var kept: T? = value
		return kept

	func keep_tuple(value) -> (int, T):
		# A tuple erases to an untyped Array that describes none of its slots, so this one is left
		# unchecked instead of carrying a check that only asserts "this is an Array".
		var kept: (int, T) = value
		return kept

	func keep_in_lambda(value):
		# The lambda never spells `self`, but its `T` slot is checked against the receiver, so it has
		# to capture the instance anyway.
		var keeper := func(v):
			var kept: T = v
			return kept
		return keeper.call(value)


# An inherited body still resolves against the leaf's specialization, not against the parameter the
# declaring class left open.
class IntCrate extends Crate[int]:
	pass


class Relay[U] extends Crate[U]:
	pass


# A trait body is compiled into each implementer but keeps naming the TRAIT's parameters, so its
# slots are resolved through the arguments that implementer applied rather than through its own
# parameter list -- which may be named differently, ordered differently, or absent entirely.
trait Keeper[V]:
	func keep_via_trait(value) -> V:
		var kept: V = value
		return kept


class TraitCrate[W]:
	uses Keeper[W]


class ConcreteTraitCrate:
	uses Keeper[int]


# The implementer declares a parameter that shares the trait parameter's name and ordinal while
# applying the trait with something else entirely.
class ShadowingTraitCrate[V]:
	uses Keeper[int]


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(5))
	print(crate.replace(1, 2))
	print(crate.collect(3))

	var inherited := IntCrate.new()
	print(inherited.keep(11))

	var relayed := Relay[String].new()
	print(relayed.keep("kept"))

	print(crate.keep_optional(null))
	print(crate.keep_optional(6))
	print(crate.keep_tuple((1, 2)))
	print(crate.keep_in_lambda(9))

	var via_trait := TraitCrate[int].new()
	print(via_trait.keep_via_trait(13))
	print(ConcreteTraitCrate.new().keep_via_trait(14))
	print(ShadowingTraitCrate[String].new().keep_via_trait(15))

	# A raw, un-parameterized receiver carries no reified argument, so the slot stays gradual.
	var raw := Crate.new()
	print(raw.keep("anything"))
	print("class parameter local and return ok")
