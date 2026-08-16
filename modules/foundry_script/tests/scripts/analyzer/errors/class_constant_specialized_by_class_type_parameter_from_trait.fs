# A trait declares a template rather than storage, so a constant bound to `Holder[V]` inside a generic
# trait is fine: every implementer flattens it into a slot of its own. An implementer that applies the
# trait with one of its *own* type parameters, directly or nested inside a composite argument, is what
# reintroduces the shared slot.
class Holder[T]:
	var value: T


trait Aliasing[V]:
	const Aliased = Holder[V]


class Forwarding[U]:
	uses Aliasing[U]


class DependentForwarding[V]:
	uses Aliasing[Array[V]]


class Fixed:
	uses Aliasing[int]


func test() -> void:
	print(Forwarding, DependentForwarding, Fixed.Aliased.new() != null)
