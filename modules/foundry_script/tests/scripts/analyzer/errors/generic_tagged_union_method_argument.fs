# A specialized static method checks its arguments against the concrete parameter type.
enum Option[T]:
	None
	Some(value: T)

	static func some(value: T) -> Option:
		return Option.Some(value)

func build() -> void:
	var wrong := Option[int].some("wrong")
	print(wrong)
