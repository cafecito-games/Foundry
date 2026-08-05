class_name LspGenericTaggedUnions

class Outer[ClassT]:
	enum Choice[EnumT: ClassT]:
		Value(value: EnumT)
		Fallback(outer: ClassT)

class Shadow[T]:
	var held: T

	enum Choice[T]:
		Value(value: T)

		static func identity[T](value: T) -> T:
			return value

enum Bounded[T: Resource, U: T]:
	Pair(first: T, second: U)

enum Recursive[T: Recursive]:
	Value(value: T)
