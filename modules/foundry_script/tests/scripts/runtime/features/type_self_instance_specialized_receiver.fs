# A specialized receiver resolves `Self` to the reified leaf. A generic `Crate[T]` instance resolves
# `Self` to its reified specialization, and a non-generic leaf fixed to a concrete argument
# (`IntCrate extends Crate[int]`) resolves `Self` to the leaf itself -- never the generic base. The
# signature checks are routed through `Object::call` so the call gets a runtime receiver to resolve
# `Self` against, the way any engine caller does.
class Crate[T]:
	var value: T

	# `Self` in a signature: the parameter is validated against the receiver's reified leaf.
	func accept(other: Self) -> bool:
		return other != null


# A non-generic leaf fixed to `int`. Its `Self` is the leaf itself, never `Crate[int]`.
class IntCrate extends Crate[int]:
	pass


func test() -> void:
	# An `IntCrate` receiver resolves `Self` to `IntCrate`. A same-leaf `IntCrate` is accepted.
	var leaf: Object = IntCrate.new()
	print("intcrate accepts intcrate: ", leaf.call("accept", IntCrate.new()) == true)

	# A `Crate[int]` is the generic base of `IntCrate`, not an `IntCrate` itself, so the `Self`
	# signature rejects it. The diagnostic identifies the receiver's leaf, not `Self` or the base.
	leaf.call("accept", Crate[int].new())
