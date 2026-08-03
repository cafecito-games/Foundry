# A recursive self reference must spell the declaration's full parameter vector.
enum Pair[T, U]:
	Both(first: T, second: U)
	Nested(inner: Pair[T])
