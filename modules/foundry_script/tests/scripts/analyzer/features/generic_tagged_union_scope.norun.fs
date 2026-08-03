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
