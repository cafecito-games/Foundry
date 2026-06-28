# A generic method can construct a `Dictionary[K, V]` value in its body: the key type parameter is
# erased at runtime, so indexing/key-assigning by a `K`-typed key and the dictionary literal form both
# analyze. The method returns an untyped dictionary at runtime (key/value erased); assigning that result
# to a concrete typed dictionary at the call site retypes it, so the target is a genuinely typed
# `Dictionary[int, String]`. Assigning to an untyped `Dictionary` target keeps the untyped value.
func pair[K, V](key: K, value: V) -> Dictionary[K, V]:
	var result: Dictionary[K, V] = {}
	result[key] = value
	return result


func pair_literal[K, V](key: K, value: V) -> Dictionary[K, V]:
	return {key: value}


func test() -> void:
	var built: Dictionary[int, String] = pair(1, "one")
	print(built, " ", built.get_typed_key_builtin() == TYPE_INT, " ", built.get_typed_value_builtin() == TYPE_STRING)

	var reassigned: Dictionary[int, String]
	reassigned = pair(2, "two")
	print(reassigned, " ", reassigned.get_typed_key_builtin() == TYPE_INT)

	var from_literal: Dictionary[int, String] = pair_literal(3, "three")
	print(from_literal, " ", from_literal.get_typed_key_builtin() == TYPE_INT)

	var untyped: Dictionary = pair(9, "nine")
	print(untyped, " ", untyped.get_typed_key_builtin() == TYPE_NIL)
	print("generic dictionary construct ok")
