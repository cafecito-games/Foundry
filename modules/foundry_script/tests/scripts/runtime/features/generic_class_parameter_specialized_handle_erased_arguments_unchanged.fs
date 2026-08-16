# "Fails to resolve" means a type-parameter node specifically, not merely "not exactly known". A
# nullable argument and a tuple argument have always erased into a plain container type at
# construction -- `Holder[int?]` reifies as `Holder[int]`, a tuple argument as the untyped Array it
# already is -- and reifying the parameter position must not change either.
class Holder[T]:
	var value: T


class Nullable:
	var holder := Holder[int?].new()


class Tupled:
	var holder := Holder[(int, String)].new()


func test() -> void:
	var nullable: Variant = Nullable.new().holder
	print(nullable is Holder[int])
	nullable.value = 1
	print(nullable.value)

	var tupled: Variant = Tupled.new().holder
	print(tupled is Holder[Array])
	tupled.value = [1, "a"]
	print(tupled.value)
	print("erased arguments unchanged ok")
