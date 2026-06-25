# An implementation must match the required generic signature's shape up to renaming; a bare `V`
# return does not satisfy a required `Array[U]` return.
trait Mapper:
	@abstract func transform[U](value: U) -> Array[U]


class Impl:
	uses Mapper

	func transform[V](value: V) -> V:
		return value
