# A decorated spelling of the declaration's own parameter is an application, not the open self type.
enum Slot[T]:
	Value(value: T)
	Nested(inner: Slot[T?])
