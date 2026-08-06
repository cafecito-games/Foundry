# A structural type argument is still just one argument: a tuple type spelled in value position does
# not change how many arguments a generic class declares.
class Box[T]:
	var value: T


func test() -> void:
	var handle = Box[(int, String), int]
	print(handle)
