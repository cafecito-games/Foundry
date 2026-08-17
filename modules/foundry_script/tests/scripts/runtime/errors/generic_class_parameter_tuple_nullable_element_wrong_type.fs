# A nullable element admits null, not anything: once the receiver resolves its parameter, a non-null
# value of the wrong type is rejected like any other. Whether some sibling element also names a
# parameter changes nothing -- a tuple element carries its own "or null" into the compiled descriptor,
# so it is evidence on its own -- and both spellings below give the same answer.
class OptionalPair[K, V]:
	func keep_optional(value) -> (K, V?):
		var kept: (K, V?) = value
		return kept


class OnlyOptional[V]:
	func keep_optional(value) -> void:
		var kept: (int, V?) = value
		print(kept)

	func keep_optional_in_lambda(value) -> void:
		# The lambda captures its receiver for exactly this check, so the element is resolved and
		# rejected inside the lambda's own frame too.
		var keeper := func(v):
			var kept: (int, V?) = v
			return kept
		print(keeper.call(value))


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print(OptionalPair[int, String].new().keep_optional(supply((1, 2))))
	OnlyOptional[String].new().keep_optional(supply((1, 2)))
	OnlyOptional[String].new().keep_optional_in_lambda(supply((1, 2)))
