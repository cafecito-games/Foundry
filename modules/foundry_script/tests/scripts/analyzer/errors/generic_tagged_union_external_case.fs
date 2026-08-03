# A generic tagged union's cases are not reachable through a bare external reference either.
enum Holder[T]:
	Value(value: T)

func check(value: Variant) -> bool:
	return value is Holder.Value
