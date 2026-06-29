# Assigning a generic method's typed-dictionary return to a wrong concrete dictionary is still rejected
# at analysis time: the substituted return type `Dictionary[int, String]` is not compatible with
# `Dictionary[int, int]`.
func pair[K, V](key: K, value: V) -> Dictionary[K, V]:
	var result: Dictionary[K, V] = {}
	result[key] = value
	return result


func test() -> void:
	var bad: Dictionary[int, int] = pair(1, "one")
	print(bad)
