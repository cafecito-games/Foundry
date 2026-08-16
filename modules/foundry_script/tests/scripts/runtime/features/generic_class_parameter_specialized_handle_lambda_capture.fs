# A construction that resolves a class type parameter needs the instance even when the body never
# names `self`, so a lambda containing one captures its receiver -- otherwise the construction would
# run with no receiver and silently degrade to the unspecialized form.
#
# The capture is derived from what the construction actually emits, so a lambda that only builds a
# concrete specialization takes none, and the instance stays collectable instead of being trapped in a
# cycle with the Callable it owns.
class Holder[T]:
	var value: T


class Building[U]:
	var maker

	func setup() -> void:
		maker = func():
			return Holder[U].new()


class ConcreteBuilding[U]:
	var maker

	func setup() -> void:
		maker = func():
			return Holder[int].new()


func test() -> void:
	var building := Building[String].new()
	building.setup()
	var made: Variant = building.maker.call()
	print(made is Holder[String])
	print(made is Holder[int])

	var concrete := ConcreteBuilding[String].new()
	concrete.setup()
	var weak_concrete: WeakRef = weakref(concrete)
	var concrete_made: Variant = concrete.maker.call()
	print(concrete_made is Holder[int])
	concrete = null
	print(weak_concrete.get_ref() == null)
	print("specialized handle lambda capture ok")
