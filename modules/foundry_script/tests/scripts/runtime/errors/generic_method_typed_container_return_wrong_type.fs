# Assigning a generic method's typed-container return to a wrong concrete container is still rejected
# at analysis time: the substituted return type `Array[int]` is not compatible with `Array[String]`.
func singleton[T](value: T) -> Array[T]:
	var result: Array[T] = []
	result.append(value)
	return result


func test() -> void:
	var bad: Array[String] = singleton(7)
	print(bad)
