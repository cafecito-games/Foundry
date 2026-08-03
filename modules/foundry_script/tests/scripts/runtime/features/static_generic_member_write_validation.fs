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


# A raw `extends Box` (no type arguments) leaves Box's own `T` unresolved for every subclass in
# this branch, forever: `RawDerived` declares no type parameters of its own to specialize it with
# later. That unresolved ordinal must never be reinterpreted as an index into a subclass's own,
# unrelated type parameters (even when the ordinals happen to collide, as they do here with `U`).
class RawDerived[U] extends Box:
	pass


# A trait member whose type still mentions an open parameter after being fixed by the `uses`
# argument (`Box[V]` below, fixed through `uses Slotted[Box[V]]`) is erased at bind time and cannot
# be validated soundly, so it must be left untyped rather than rejected.
class DependentHolder[V]:
	uses Slotted[Box[V]]


# A dependent argument that is itself a class handle (`Type[Box[V]]`) has a sound upper bound at its
# root (`Type[Box]`, any handle for a `Box`) even though `V` is erased, so the write must still be
# validated (a wrong represented class, e.g. `Type[NotBox]`, is rejected elsewhere), but the nested,
# unrecoverable `V` argument must not trip an invariant mismatch against a real `Box[int]` handle.
class DependentHandleHolder[V]:
	uses Slotted[Type[Box[V]]]


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

	var raw_derived := RawDerived[String].new()
	var dynamic_raw_derived: Variant = raw_derived
	dynamic_raw_derived.value = 8
	print(raw_derived.value)

	var dependent_holder := DependentHolder[int].new()
	var dynamic_dependent: Variant = dependent_holder
	var payload := Box[int].new()
	payload.value = 9
	dynamic_dependent.slot = payload
	@warning_ignore("unsafe_property_access")
	print(dependent_holder.slot.value)

	var dependent_handle_holder := DependentHandleHolder[int].new()
	var dynamic_dependent_handle: Variant = dependent_handle_holder
	dynamic_dependent_handle.slot = Box[int]
	print(dependent_handle_holder.slot == Box[int])
