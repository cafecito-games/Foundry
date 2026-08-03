# An applied argument has to satisfy the bound declared for its parameter.
enum Bounded[T: Resource]:
	Value(value: T)

func take(value: Bounded[int]) -> void:
	print(value)
