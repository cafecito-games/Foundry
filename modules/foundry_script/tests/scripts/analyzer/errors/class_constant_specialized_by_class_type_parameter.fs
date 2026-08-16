# A constant is folded once into a constant pool -- the class's for a member, the compiled function's
# for a local -- with no receiver to reify a type parameter against. Binding one to a specialized
# class handle whose arguments name a class type parameter would give every specialization the same
# handle, which is the identical unsoundness a static variable typed by a class parameter has, so the
# declaration is rejected outright. An annotation that hides the handle does not change what the
# constant slot holds, so it does not change the answer either.
class Holder[T]:
	var value: T


class Wrapper[U]:
	const Aliased = Holder[U]


class NestedWrapper[U]:
	const Nested = Holder[Array[U]]


class HiddenWrapper[U]:
	const Hidden: Variant = Holder[U]


class LocalWrapper[U]:
	func build() -> Variant:
		const Aliased = Holder[U]
		return Aliased.new()


# A hard annotation that names no specialization widens it away, so the shared constant claims
# nothing about the parameter and stays legal.
class WidenedWrapper[U]:
	const Widened: FoundryScript = Holder[U]


class Fixed:
	const Concrete = Holder[int]

	func build() -> Variant:
		const Local = Holder[int]
		return Local.new()


func test() -> void:
	print(Wrapper, NestedWrapper, HiddenWrapper, LocalWrapper)
	print(WidenedWrapper[int].new().Widened != null)
	print(Fixed.Concrete.new() != null, Fixed.new().build() != null)
