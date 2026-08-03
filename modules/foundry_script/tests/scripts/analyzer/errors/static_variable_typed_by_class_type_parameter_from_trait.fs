# A trait declares a template rather than storage, so `static var slot: T` inside a generic trait is
# fine: every implementer flattens the member into a slot of its own. What is not fine is an
# implementer that applies the trait with one of its *own* type parameters, directly or nested inside
# a composite argument: the flattened slot is then typed by the implementer's parameter and shared by
# all of its specializations, which is the same unsound storage the class-declared form has.
trait Slotted[T]:
	static var slot: T


class Box[V]:
	pass


class Forwarding[U]:
	uses Slotted[U]


class DependentForwarding[V]:
	uses Slotted[Box[V]]


class Fixed:
	uses Slotted[int]


func test() -> void:
	Fixed.slot = 1
	print(Fixed.slot, Forwarding, DependentForwarding)
