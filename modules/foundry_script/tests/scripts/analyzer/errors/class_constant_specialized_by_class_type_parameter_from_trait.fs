# A trait declares a template rather than storage, so a constant bound to `Holder[V]` inside a generic
# trait is fine: every implementer flattens it into a slot of its own. An implementer that applies the
# trait with one of its *own* type parameters, directly or nested inside a composite argument, is what
# reintroduces the shared slot. `Self` on a generic implementer is the same thing spelled differently:
# it carries the class's own parameters as its arguments, so `final` does not make it foldable either.
# `Self` reaches the constant by two spellings -- the implementer applies it, or the trait writes it
# directly in the constant -- and both are rejected on a generic implementer, generic trait or not.
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


trait DirectAliasing:
	const Direct = Holder[Self]


class DirectGeneric[Y]:
	uses DirectAliasing


final class ClosedDirectGeneric[Z]:
	uses DirectAliasing


trait GenericDirectAliasing[P]:
	const Direct = Holder[Self]


class GenericTraitDirectGeneric[Q]:
	uses GenericDirectAliasing[int]


func test() -> void:
	print(Forwarding, DependentForwarding, Fixed.Aliased.new() != null)
