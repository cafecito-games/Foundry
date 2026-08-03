# A static member typed as a class generic parameter is validated at runtime the same way an
# instance member is: against a binding fixed by a trait/base specialization, against the reified
# argument of the instance a static write is routed through, or (for a member inherited from a
# generic base) against the subclass's own specialization projected through the ancestor chain.
# Well-typed values (including ones that convert) are accepted on every reachable write path.
trait Slotted[T]:
	static var slot: T


class Box[T]:
	static var value: T


class Holder:
	uses Slotted[int]


class IntBox extends Box[int]:
	pass


func test() -> void:
	Holder.slot = 1
	print(Holder.slot)

	var dynamic_class: Variant = Holder
	dynamic_class.slot = 2
	print(Holder.slot)

	var box := Box[float].new()
	var dynamic_instance: Variant = box
	dynamic_instance.value = 3
	print(box.value)
	dynamic_instance.value = 4.5
	print(box.value)

	var dynamic_inherited_class: Variant = IntBox
	dynamic_inherited_class.value = 6
	print(IntBox.value)

	var inherited_box := IntBox.new()
	var dynamic_inherited_instance: Variant = inherited_box
	dynamic_inherited_instance.value = 7
	print(inherited_box.value)
