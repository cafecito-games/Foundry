# A static member typed as a generic trait's type parameter and fixed by the implementer's `uses`
# argument is validated at runtime the same way an instance member is. The trait member is flattened
# into a slot the implementer owns, so the fixed argument is the sound expectation for every write
# path that reaches it: through the class, through a dynamic reference to the class, or through an
# instance. Well-typed values (including ones that convert) are accepted on every one of them.
#
# The implementer must fix the argument concretely; applying the trait with one of its own type
# parameters is rejected by the analyzer, because a single static slot cannot be typed differently
# per specialization (see `analyzer/errors/static_variable_typed_by_class_type_parameter*`).
trait Slotted[T]:
	static var slot: T


class Holder:
	uses Slotted[int]


class FloatHolder:
	uses Slotted[float]


func test() -> void:
	Holder.slot = 1
	print(Holder.slot)

	var dynamic_class: Variant = Holder
	dynamic_class.slot = 2
	print(Holder.slot)

	var holder := Holder.new()
	var dynamic_instance: Variant = holder
	dynamic_instance.slot = 3
	print(Holder.slot)

	# A value that converts to the fixed argument is accepted, and the two implementers keep
	# independent storage.
	var dynamic_float_class: Variant = FloatHolder
	dynamic_float_class.slot = 4
	print(FloatHolder.slot, " ", Holder.slot)
