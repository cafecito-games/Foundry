# A specialized declaration checks the value's nominal identity unconditionally and its type
# arguments only against the evidence the value carries, so a value whose arguments for that class or
# trait are absent is stored. `is` asks the stronger question and rejects that same value, so every
# case prints the store answer and the narrowing answer as an adjacent pair: the store reads `true`
# where the test reads `false`.
class Box[T]:
	var value: T


class DerivedBox[U] extends Box[U]:
	pass


trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class ForwardingKeeper[U]:
	uses Keeper[U]


class StoringKeeper[U]:
	uses Storing[U]


func test() -> void:
	var class_variant: Variant = Box.new()
	var class_variant_slot: Box[int] = class_variant
	print("class variant store: ", class_variant_slot != null)
	print("class variant test: ", class_variant is Box[int])

	var class_erased := Box.new()
	var class_erased_slot: Box[int] = class_erased
	print("class erased store: ", class_erased_slot != null)
	print("class erased test: ", class_erased is Box[int])

	var trait_variant: Variant = ForwardingKeeper.new()
	var trait_variant_slot: Keeper[int] = trait_variant
	print("trait variant store: ", trait_variant_slot != null)
	print("trait variant test: ", trait_variant is Keeper[int])

	var trait_erased := ForwardingKeeper.new()
	var trait_erased_slot: Keeper[int] = trait_erased
	print("trait erased store: ", trait_erased_slot != null)
	print("trait erased test: ", trait_erased is Keeper[int])

	# Exact evidence answers the same way at both boundaries.
	var class_exact := Box[int].new()
	var class_exact_slot: Box[int] = class_exact
	print("class exact store: ", class_exact_slot != null)
	print("class exact test: ", class_exact is Box[int])

	var trait_exact := ForwardingKeeper[int].new()
	var trait_exact_slot: Keeper[int] = trait_exact
	print("trait exact store: ", trait_exact_slot != null)
	print("trait exact test: ", trait_exact is Keeper[int])

	# A subclass projection and a supertrait reach the same answers as the direct forms.
	var subclass_exact := DerivedBox[int].new()
	var subclass_exact_slot: Box[int] = subclass_exact
	print("subclass exact store: ", subclass_exact_slot != null)
	print("subclass exact test: ", subclass_exact is Box[int])

	var subclass_erased: Variant = DerivedBox.new()
	var subclass_erased_slot: Box[int] = subclass_erased
	print("subclass erased store: ", subclass_erased_slot != null)
	print("subclass erased test: ", subclass_erased is Box[int])

	var supertrait_exact := StoringKeeper[int].new()
	var supertrait_exact_slot: Keeper[int] = supertrait_exact
	print("supertrait exact store: ", supertrait_exact_slot != null)
	print("supertrait exact test: ", supertrait_exact is Keeper[int])

	var supertrait_erased: Variant = StoringKeeper.new()
	var supertrait_erased_slot: Keeper[int] = supertrait_erased
	print("supertrait erased store: ", supertrait_erased_slot != null)
	print("supertrait erased test: ", supertrait_erased is Keeper[int])

	# A raw destination asks only the nominal question, so an argument-erased source stays accepted.
	var raw_class_slot: Box = Box.new()
	print("raw class store: ", raw_class_slot != null)
	print("raw class test: ", Box.new() is Box)

	var raw_trait_slot: Keeper = ForwardingKeeper.new()
	print("raw trait store: ", raw_trait_slot != null)
	print("raw trait test: ", ForwardingKeeper.new() is Keeper)
