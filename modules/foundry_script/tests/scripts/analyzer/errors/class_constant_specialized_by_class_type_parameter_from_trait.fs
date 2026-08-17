# A trait declares a template rather than storage, so a constant bound to `Holder[V]` inside a generic
# trait is fine: every implementer flattens it into a slot of its own. An implementer that applies the
# trait with one of its *own* type parameters, directly or nested inside a composite argument, is what
# reintroduces the shared slot. `Self` on a generic implementer is the same thing spelled differently:
# it carries the class's own parameters as its arguments, so `final` does not make it foldable either.
class Holder[T]:
	var value: T


trait Aliasing[V]:
	const Aliased = Holder[V]


class Forwarding[U]:
	uses Aliasing[U]


class DependentForwarding[V]:
	uses Aliasing[Array[V]]


final class ClosedSelf[W]:
	uses Aliasing[Self]


class OpenSelf[X]:
	uses Aliasing[Array[Self]]


class Fixed:
	uses Aliasing[int]


func test() -> void:
	print(Forwarding, DependentForwarding, Fixed.Aliased.new() != null)
