# `Self` in a witness reifies to the conformance target for every target kind, and here the target is
# observably different from the script that owns the compiled witness: this file's own runtime base
# is RefCounted, while the targets are the native class Node and a Foundry Script class of this file.
# Reifying against the owner instead would type the `T` slot as this file's base and reject the
# value the witness stores.
extends RefCounted


class Crate[T]:
	var value: T

	func store(next: T) -> void:
		value = next

	func stored() -> bool:
		return value != null


class Holder:
	var badge: String = "holder"


trait Cratable:
	abstract func created() -> Crate[Self]


extend Node uses Cratable:
	func created() -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		made.store(self)
		return made


extend Holder uses Cratable:
	func created() -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		made.value = self
		return made


func test() -> void:
	var node := Node.new()
	var node_cratable: Cratable = node
	print(node_cratable.created().stored())
	node.free()

	var holder_cratable: Cratable = Holder.new()
	print(holder_cratable.created().stored())
