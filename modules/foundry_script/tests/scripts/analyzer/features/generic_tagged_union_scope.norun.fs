# A generic tagged union's parameters sit between its own methods and its declaring class, so the
# same spelling can name three different parameters in one lexical chain.
class Outer[ClassT]:
	enum Choice[EnumT: ClassT]:
		Value(value: EnumT)
		Fallback(outer: ClassT)

		static func identity[MethodT](value: MethodT) -> MethodT:
			var through_lambda := func(inner: MethodT) -> MethodT:
				return inner
			print(through_lambda)
			return value

class Shadow[T]:
	var held: T

	enum Choice[T]:
		Value(value: T)

		static func identity[T](value: T) -> T:
			return value

enum Bounded[T: Resource, U: T]:
	Pair(first: T, second: U)

# An explicit Variant bound does not change the parameter's identity, and a bound may name the
# union being declared because its open identity is published before bounds are resolved.
enum Slot[T: Variant]:
	Value(value: T)
	Nested(inner: Slot[T])

enum Recursive[T: Recursive]:
	Value(value: T)

# A union's open arguments belong to the union, so calling an enum function through the open self
# type still sees the enclosing class's own parameters.
class Host[ClassT]:
	enum Choice[EnumT]:
		Value(value: EnumT)

		static func identity(value: ClassT) -> ClassT:
			return value

		static func call_it(value: ClassT) -> ClassT:
			return Choice.identity(value)
