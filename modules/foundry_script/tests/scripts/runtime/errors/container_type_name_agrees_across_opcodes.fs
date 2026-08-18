# A specialized class is named in full wherever it is reported. The element of a typed-array assign,
# the value of a typed-dictionary assign, and a member store all describe the same declared `Box[int]`,
# so all three have to spell its argument: naming the element with a renderer of its own is how the
# assign paths used to degrade `Array[Box[int]]` to `Array[Box]` while the member store spelled it out.
class Box[T]:
	var value: T


class Holder[T]:
	var kept: T

	func keep(value: Variant) -> void:
		kept = value


func supply(value: Variant) -> Variant:
	return value


func assign_array() -> void:
	var kept: Array[Box[int]] = supply(7)
	print("array: ", kept)


func assign_dictionary() -> void:
	var kept: Dictionary[String, Box[int]] = supply(7)
	print("dictionary: ", kept)


func assign_variant_argument() -> void:
	# An explicit `Variant` argument is carried as the default/NIL carrier, and the assign path has to
	# spell that carrier `Variant` rather than leaking the internal `Nil` name into the diagnostic.
	var kept: Array[Box[Variant]] = supply(7)
	print("variant argument: ", kept)


func store_member() -> void:
	var holder := Holder[Box[int]].new()
	holder.keep(7)
	print("member: ", holder.kept)


func test() -> void:
	assign_array()
	assign_dictionary()
	assign_variant_argument()
	store_member()
