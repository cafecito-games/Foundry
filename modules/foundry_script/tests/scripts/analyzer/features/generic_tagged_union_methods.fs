# Specialized enum methods type-check static, instance, and generic method bodies against the
# receiver's applied arguments rather than the declaration's open parameters.
enum Option[T]:
	None
	Some(value: T)

	static func some(value: T) -> Option:
		return Option.Some(value)

	func is_some() -> bool:
		return self is Option.Some(_)

	static func echo[U](value: U) -> U:
		return value

func test():
	print(Option[int].some(5).is_some())
	print(Option[int].Some(2).is_some())
	print(Option[int].echo("typed"))
