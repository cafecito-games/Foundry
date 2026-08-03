# A static member typed as a class generic parameter is validated at runtime the same way an
# instance member is: against a binding fixed by a trait/base specialization, or against the
# reified argument of the instance a static write is routed through. Well-typed values (including
# ones that convert) are accepted on every reachable write path.
trait Slotted[T]:
	static var slot: T


class Box[T]:
	static var value: T


class Holder:
	uses Slotted[int]


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
