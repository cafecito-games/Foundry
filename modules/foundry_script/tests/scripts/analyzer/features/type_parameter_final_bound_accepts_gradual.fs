# A parameter bounded by a `final` class denotes exactly that class: no subtype of the bound can
# exist, so a value the bound accepts is a value every possible argument accepts. The erasure
# argument that refuses a gradual source into a bare parameter therefore does not apply, at any depth
# and in any frame.
final class FbgLabel:
	var text: String = "label"

	func read() -> String:
		return text


func bare_local[T: FbgLabel](value: Variant) -> String:
	var kept: T = value
	return kept.read()


func nested_local[T: FbgLabel](value: Variant) -> int:
	var kept: Array[T] = value
	return kept.size()


func gradual_return[T: FbgLabel](value: Variant) -> T:
	return value


func literal_element[T: FbgLabel](value: Variant) -> int:
	var kept: Array[T] = [value]
	return kept.size()


func later_store[T: FbgLabel](initial: T, value: Variant) -> String:
	var kept: T = initial
	kept = value
	return kept.read()


func gradual_parameter[T: FbgLabel](value: Variant) -> String:
	return bare_local[T](value)


func nullable_local[T: FbgLabel](value: Variant) -> bool:
	var kept: T? = value
	return kept == null


func dictionary_slots[T: FbgLabel](key: Variant, value: Variant) -> int:
	var keyed: Dictionary[T, String] = { key: "first" }
	var valued: Dictionary[String, T] = { "first": value }
	return keyed.size() + valued.size()


func tuple_element[T: FbgLabel](value: Variant) -> String:
	# A tuple keeps its element shape in the compiled descriptor, so a directly named element resolves
	# to the bound exactly as a bare declaration does.
	var kept: (int, T) = (1, value)
	return kept[1].read()


func type_handle[T: FbgLabel](value: Variant) -> String:
	# The handle layer travels beside the resolved bound, so the slot still demands a class handle.
	var kept: Type[T] = value
	return kept.new().read()


class Holder[T: FbgLabel]:
	static func static_bare_local(value: Variant) -> String:
		var kept: T = value
		return kept.read()

	static func static_nested_local(value: Variant) -> int:
		var kept: Array[T] = value
		return kept.size()

	static func static_gradual_return[U: FbgLabel](value: Variant) -> U:
		return value


func untyped_label() -> Variant:
	return FbgLabel.new()


func untyped_labels() -> Variant:
	var labels: Array[FbgLabel] = [FbgLabel.new()]
	return labels


func untyped_label_handle() -> Variant:
	return FbgLabel


func test() -> void:
	print(bare_local[FbgLabel](untyped_label()))
	print(nested_local[FbgLabel](untyped_labels()))
	print(gradual_return[FbgLabel](untyped_label()).read())
	print(literal_element[FbgLabel](untyped_label()))
	print(later_store[FbgLabel](FbgLabel.new(), untyped_label()))
	print(gradual_parameter[FbgLabel](untyped_label()))
	print(nullable_local[FbgLabel](untyped_label()))
	print(dictionary_slots[FbgLabel](untyped_label(), untyped_label()))
	print(tuple_element[FbgLabel](untyped_label()))
	print(type_handle[FbgLabel](untyped_label_handle()))
	print(Holder[FbgLabel].static_bare_local(untyped_label()))
	print(Holder[FbgLabel].static_nested_local(untyped_labels()))
	print(Holder[FbgLabel].static_gradual_return[FbgLabel](untyped_label()).read())
