# A `Self` target resolves to the frame's exact receiver first and then runs the ordinary specialized
# predicate. A static frame takes the receiver the call was made through; an instance frame takes the
# running instance's leaf specialization. A nested `Self` position keeps the resolved arguments at its
# own depth, so `Crate[Self]` means `Crate[Crate[int]]` for a receiver of `Crate[int]`.
class Crate[T]:
	var item: T

	static func static_matches(value: Variant) -> bool:
		return value is Self

	static func static_cast_is_null(value: Variant) -> bool:
		return (value as Self) == null

	static func static_matches_nested(value: Variant) -> bool:
		return value is Crate[Self]

	func instance_matches(value: Variant) -> bool:
		return value is Self

	func instance_cast_is_null(value: Variant) -> bool:
		return (value as Self) == null


func test() -> void:
	var int_crate: Variant = Crate[int].new()
	var string_crate: Variant = Crate[String].new()
	var raw_crate: Variant = Crate.new()

	print("static Self accepts matching: ", Crate[int].static_matches(int_crate))
	print("static Self rejects mismatch: ", Crate[int].static_matches(string_crate))
	print("static Self rejects raw: ", Crate[int].static_matches(raw_crate))
	print("static Self cast keeps matching: ", Crate[int].static_cast_is_null(int_crate) == false)
	print("static Self cast nulls mismatch: ", Crate[int].static_cast_is_null(string_crate))

	var nested: Variant = Crate[Crate[int]].new()
	var nested_mismatch: Variant = Crate[Crate[String]].new()
	print("nested Self accepts matching: ", Crate[int].static_matches_nested(nested))
	print("nested Self rejects mismatch: ", Crate[int].static_matches_nested(nested_mismatch))

	var receiver := Crate[int].new()
	print("instance Self accepts matching: ", receiver.instance_matches(int_crate))
	print("instance Self rejects mismatch: ", receiver.instance_matches(string_crate))
	print("instance Self rejects raw: ", receiver.instance_matches(raw_crate))
	print("instance Self cast keeps matching: ", receiver.instance_cast_is_null(int_crate) == false)
	print("instance Self cast nulls mismatch: ", receiver.instance_cast_is_null(string_crate))

	var string_receiver := Crate[String].new()
	print("string receiver accepts string: ", string_receiver.instance_matches(string_crate))
	print("string receiver rejects int: ", string_receiver.instance_matches(int_crate))

	print("is/as Self receiver ok")
