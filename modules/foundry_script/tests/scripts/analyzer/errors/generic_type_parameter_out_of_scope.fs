# A class type parameter (`T`) is only a valid type name inside the class that declares it.
# Referencing the bare parameter from an unrelated scope (a top-level function) must fail to
# resolve, proving type parameters are not globally visible type identifiers.
class Box[T]:
	var value: T


func test() -> void:
	var stray: T = null
	print(stray)
