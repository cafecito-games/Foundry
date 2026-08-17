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


func test() -> void:
	print(bare_local[FbgLabel](untyped_label()))
	print(nested_local[FbgLabel](untyped_labels()))
	print(gradual_return[FbgLabel](untyped_label()).read())
	print(literal_element[FbgLabel](untyped_label()))
	print(Holder[FbgLabel].static_bare_local(untyped_label()))
	print(Holder[FbgLabel].static_nested_local(untyped_labels()))
	print(Holder[FbgLabel].static_gradual_return[FbgLabel](untyped_label()).read())
