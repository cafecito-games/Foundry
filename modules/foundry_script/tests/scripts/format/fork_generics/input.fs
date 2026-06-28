class Pair[K,V: RefCounted]:
	var key: K
	var value: V
func swap[T](a: T,b: T) -> Array[T]:
	return [b,a]
