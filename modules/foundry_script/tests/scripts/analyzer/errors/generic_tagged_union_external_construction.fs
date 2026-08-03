# Constructing a case through the bare union outside its declaration is an external use.
enum Holder[T: Resource]:
	Value(value: T)

func leak() -> Variant:
	return Holder.Value(123)
