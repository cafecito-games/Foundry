# A class constant has exactly one slot, materialized once for the declaring class with no receiver to
# reify a type parameter against. Binding it to a specialized class handle whose arguments name a
# class type parameter would give every specialization the same handle -- the identical unsoundness a
# static variable typed by a class parameter has -- so the declaration is rejected outright.
class Holder[T]:
	var value: T


class Wrapper[U]:
	const Aliased = Holder[U]


class NestedWrapper[U]:
	const Nested = Holder[Array[U]]


class Fixed:
	const Concrete = Holder[int]


func test() -> void:
	print(Wrapper, NestedWrapper, Fixed.Concrete.new() != null)
