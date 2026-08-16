# A lambda captures its receiver only when something inside it actually needs one. A slot that gets
# no receiver check is not a reason to capture: doing so would buy nothing and cost a reference cycle,
# since the receiver owns the Callable while the Callable holds a strong reference back. Freeing the
# last outside reference has to actually free the instance.
#
# A tuple gets no check because it erases to an untyped Array describing none of its slots.
class Crate[T]:
	var keeper

	func setup() -> void:
		keeper = func(v):
			var kept: (int, T) = v
			return kept


# A nullable slot gets none either: it admits null, which no container type can express, so the
# runtime keeps no evidence for it.
class NullableCrate[T]:
	var keeper

	func setup() -> void:
		keeper = func(v):
			var kept: T? = v
			return kept


# A defaulted parameter is not a checked slot: nothing validates a default against the receiver.
class DefaultedCrate[T]:
	var keeper

	func setup() -> void:
		keeper = func(v: T = null):
			return v


# A trait applied with a concrete argument leaves a shape with no parameter left in it, so the body
# checks the slot without needing a receiver at all -- and therefore without a capture.
trait LambdaKeeper[V]:
	var keeper := func(v):
		var kept: V = v
		return kept


class ConcreteLambdaKeeper:
	uses LambdaKeeper[int]


# Forwarding the implementer's own parameter does leave one, so this lambda captures and checks.
class ForwardingLambdaKeeper[W]:
	uses LambdaKeeper[W]


# The comparison case: a bare `T` slot is checked against the receiver, so this lambda does capture
# it, and the resulting cycle is the deliberate cost of the check.
class CheckedCrate[T]:
	var keeper

	func setup() -> void:
		keeper = func(v):
			var kept: T = v
			return kept


func test() -> void:
	var crate := Crate[int].new()
	crate.setup()
	var weak_crate: WeakRef = weakref(crate)
	crate = null
	print(weak_crate.get_ref() == null)

	var nullable_crate := NullableCrate[int].new()
	nullable_crate.setup()
	var weak_nullable: WeakRef = weakref(nullable_crate)
	nullable_crate = null
	print(weak_nullable.get_ref() == null)

	var defaulted := DefaultedCrate[int].new()
	defaulted.setup()
	var weak_defaulted: WeakRef = weakref(defaulted)
	defaulted = null
	print(weak_defaulted.get_ref() == null)

	var concrete_trait := ConcreteLambdaKeeper.new()
	var weak_concrete_trait: WeakRef = weakref(concrete_trait)
	print(concrete_trait.keeper.call(1))
	concrete_trait = null
	print(weak_concrete_trait.get_ref() == null)

	print(ForwardingLambdaKeeper[int].new().keeper.call(2))

	var checked := CheckedCrate[int].new()
	checked.setup()
	print(checked.keeper.call(5))
	print("lambda capture ok")
