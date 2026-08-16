# A nullable element admits null, not anything: once the receiver resolves its parameter, a non-null
# value of the wrong type is rejected like any other. The sibling element is what makes this shape
# receiver-relative -- a slot whose only parameter element is nullable keeps no evidence at all.
class OptionalPair[K, V]:
	func keep_optional(value) -> (K, V?):
		var kept: (K, V?) = value
		return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print(OptionalPair[int, String].new().keep_optional(supply((1, 2))))
