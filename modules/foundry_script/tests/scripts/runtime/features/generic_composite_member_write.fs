# A member that merely *contains* a class type parameter (`Array[T]`, either dictionary slot, deeper
# nesting) erases exactly like a bare `T` member does, so it carries a structured binding too: the
# concrete parts of the declared shape are enforced outright and the parameter leaves are resolved
# against the argument reified onto the instance. Both the internal direct store and the external
# `set()` path go through that one boundary.
class Shelf[T]:
	var items: Array[T] = []
	var by_name: Dictionary[String, T] = {}
	var by_item: Dictionary[T, String] = {}
	var grouped: Array[Dictionary[String, Array[T]]] = []

	func stock(values: Array[T]) -> void:
		items = values

	func label(entries: Dictionary[String, T]) -> void:
		by_name = entries


class IntShelf extends Shelf[int]:
	pass


trait Stocked[V]:
	var trait_items: Array[V] = []


class TraitShelf[W]:
	uses Stocked[W]


func test() -> void:
	var shelf := Shelf[int].new()
	shelf.stock([1, 2])
	print(shelf.items)
	shelf.label({ "first": 3 })
	print(shelf.by_name)
	shelf.by_item = { 4: "fourth" }
	print(shelf.by_item)
	shelf.grouped = [{ "batch": [5] }]
	print(shelf.grouped)

	# An inherited member re-resolves against the leaf's specialization.
	var inherited := IntShelf.new()
	inherited.stock([6])
	print(inherited.items)

	var via_trait := TraitShelf[int].new()
	via_trait.trait_items = [7]
	print(via_trait.trait_items)

	# A raw, un-parameterized receiver has no reified argument, so the parameter leaf stays gradual
	# while the concrete outer shape still holds.
	var raw := Shelf.new()
	raw.stock(["anything"])
	print(raw.items)
	print("composite member write ok")
