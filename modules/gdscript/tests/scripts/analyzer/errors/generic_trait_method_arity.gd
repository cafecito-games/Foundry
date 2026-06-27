# An implementation's type-parameter arity must match the required trait method's.
trait Mapper:
	abstract func transform[U](value: U) -> Array[U]


class Impl:
	uses Mapper

	func transform[V, W](value: V) -> Array[V]:
		return [value]
