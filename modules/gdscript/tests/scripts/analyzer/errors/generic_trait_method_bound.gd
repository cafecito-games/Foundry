# An implementation's type-parameter bound must align with the required trait method's.
trait Mapper:
	abstract func transform[U: RefCounted](value: U) -> Array[U]


class Impl:
	uses Mapper

	func transform[V](value: V) -> Array[V]:
		return [value]
