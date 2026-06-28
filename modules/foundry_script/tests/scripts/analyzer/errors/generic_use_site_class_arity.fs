# A multi-argument use-site type-argument list is matched against the class arity. `Box` declares a
# single type parameter, so applying two arguments is an error rather than silently dropping one.
class Box[T]:
	var value: T


func test() -> void:
	var b = Box[int, String].new()
	print(b)
