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

	var checked := CheckedCrate[int].new()
	checked.setup()
	print(checked.keeper.call(5))
	print("lambda capture ok")
