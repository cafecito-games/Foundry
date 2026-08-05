enum Option[T]:
	None
	Some(value: T)

	static func some(value: T) -> Option:
		return Option.Some(value)

	func is_some() -> bool:
		return self is Option.Some(_)

	static func echo[U](value: U) -> U:
		return value

func describe(value: Option[int]) -> String:
	if value.is_some():
		return "some"
	return "none"

func test():
	print(Option[int].some(5).is_some())
	print(Option[int].Some(2).is_some())
	print(Option[int].echo(9))
	print(describe(Option[int].some(1)))
	print(describe(Option[int].None))
