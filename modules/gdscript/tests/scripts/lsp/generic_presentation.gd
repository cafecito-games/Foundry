class Animal:
	pass

func swap[T](first: T, second: T) -> T:
	return first

func clamp_within[U: RefCounted](value: U) -> U:
	return value

func tag[A: Animal](value: A) -> A:
	return value
