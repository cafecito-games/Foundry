# A generic method returning a typed container whose element is a type parameter (`-> Array[T]` /
# `-> Dictionary[K, V]`) yields an untyped container at runtime (the element/key/value are erased).
# Member field initializers and static variable initializers must retype that result into the
# concrete container, just like local declarations and later assignments do. Without the converting
# retype the strict typed-assign opcode rejects the untyped runtime container.
static func singleton[T](value: T) -> Array[T]:
	var result: Array[T] = []
	result.append(value)
	return result


static func pair[K, V](key: K, value: V) -> Dictionary[K, V]:
	var result: Dictionary[K, V] = {}
	result[key] = value
	return result


var member_array: Array[int] = singleton(7)
var member_dictionary: Dictionary[int, String] = pair(1, "one")

static var static_array: Array[int] = singleton(5)
static var static_dictionary: Dictionary[int, String] = pair(2, "two")


func test() -> void:
	print(member_array, " ", member_array.get_typed_builtin() == TYPE_INT)
	print(member_dictionary, " ", member_dictionary.get_typed_key_builtin() == TYPE_INT, " ", member_dictionary.get_typed_value_builtin() == TYPE_STRING)
	print(static_array, " ", static_array.get_typed_builtin() == TYPE_INT)
	print(static_dictionary, " ", static_dictionary.get_typed_key_builtin() == TYPE_INT, " ", static_dictionary.get_typed_value_builtin() == TYPE_STRING)
	print("generic container return field initializer ok")
