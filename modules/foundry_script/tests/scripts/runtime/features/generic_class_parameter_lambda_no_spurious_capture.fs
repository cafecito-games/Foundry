# A lambda captures its receiver only when something inside it actually needs one. A slot whose only
# class-parameter dependency is inside a tuple gets no receiver check -- a tuple erases to an untyped
# Array that describes none of its slots -- so capturing the instance for it would buy nothing and
# cost a reference cycle: the receiver owns the Callable while the Callable holds a strong reference
# back. Freeing the last outside reference has to actually free the instance.
class Crate[T]:
	var keeper

	func setup() -> void:
		keeper = func(v):
			var kept: (int, T) = v
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

	var checked := CheckedCrate[int].new()
	checked.setup()
	print(checked.keeper.call(5))
	print("lambda capture ok")
