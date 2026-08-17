# Absent evidence is accepted at a specialized store, but known evidence that contradicts the
# declaration is rejected statically: `Box[String]` never satisfies a `Box[int]` declaration.
class Box[T]:
	var value: T


func test() -> void:
	var conflicting: Box[int] = Box[String].new()
	print(conflicting)
