# A generic tagged union has no bare form outside its own declaration.
enum Holder[T]:
	Value(value: T)

func take(held: Holder) -> void:
	print(held)
