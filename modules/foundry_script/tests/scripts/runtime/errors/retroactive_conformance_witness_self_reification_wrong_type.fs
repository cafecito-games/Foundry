# Reifying a witness `Self` to the conformance target keeps validation exact: the instance a builtin
# witness built is a `Crate[int]`, so storing a String into its `T`-typed member is still rejected.
# An erased or widened argument would accept this write instead.
class Crate[T]:
	var value: T


trait Cratable:
	abstract static func packed(value: Self) -> Crate[Self]


extend int uses Cratable:
	static func packed(value: Self) -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		made.value = value
		return made


func test() -> void:
	var crate := int.packed(7)
	var dynamic: Variant = crate
	dynamic.value = "not an int"
	print(crate.value)
