# A class type parameter is reified onto the instance, so a function-body slot declared with it --
# a local, a later store into that local, or the return -- is validated against the argument this
# receiver actually carries, without generating a separate method body per specialization. The check
# happens at the slot's own boundary, so it holds no matter what the caller consumes the result as.
# The check converts where a member store would, so a scalar slot keeps the converted value. A
# container slot is the exception: a generic method returning `Array[T]` yields a runtime array whose
# element type is erased on purpose, and its concrete consumer is what retypes it.
class Crate[T]:
	func keep(value) -> T:
		var kept: T = value
		return kept

	func replace(first, second) -> T:
		var kept: T = first
		kept = second
		return kept

	func keep_container_argument(value: Variant) -> T:
		# A bare `T` reified to a container is an ordinary slot: the check converts the value into the
		# argument, exactly as it does for any other. Only a slot *declared* as a container of a
		# parameter leaves its runtime element typing to the consumer.
		var kept: T = value
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


# A trait brings its field initializers along too, and a lambda inside one is compiled with the
# implementer's `@implicit_new()` rather than with the trait's methods; it still has to resolve
# through the arguments this implementer applied.
trait LambdaKeeper[V]:
	var keeper := func(v):
		var kept: V = v
		return kept


class ShadowingLambdaKeeper[V]:
	uses LambdaKeeper[int]


class ConcreteTraitCrate:
	uses Keeper[int]


# The implementer declares a parameter that shares the trait parameter's name and ordinal while
# applying the trait with something else entirely.
class ShadowingTraitCrate[V]:
	uses Keeper[int]


func untyped_numbers() -> Variant:
	return [1, 2]


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(5))
	print(crate.keep(7.0)) # converted to int, exactly as a member store would
	print(crate.replace(1, 2))
	print(crate.collect(3))

	var container_crate := Crate[Array[int]].new()
	var container_kept = container_crate.keep_container_argument(untyped_numbers())
	print(container_kept)
	print(container_kept.get_typed_builtin() == TYPE_INT)

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
	print(ShadowingLambdaKeeper[String].new().keeper.call(16))

	# A raw, un-parameterized receiver carries no reified argument, so the slot stays gradual.
	var raw := Crate.new()
	print(raw.keep("anything"))
	print("class parameter local and return ok")
