# Tuple element types live in `container_element_types`, so generic substitution reaches them:
# specializing `Box[T]` rewrites `(T, int)` into `(String, int)`.
class Box[T]:
	var entry: (T, int)

	func make(value: T) -> (T, int):
		return (value, 0)

func use_specialized() -> void:
	var box := Box[String].new()
	var entry: (String, int) = box.entry
	var made: (String, int) = box.make("one")
	prints(entry, made)
