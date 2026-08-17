# A tuple literal written as a call argument, as a parameter default, or as an explicitly specialized
# generic argument is built with the parameter's declared element typing. This is what makes a direct
# `take((1, []))` compile and run: the parameter boundary is strict about a container element arriving
# already typed, and the literal now arrives that way.
class Receiver extends RefCounted:
	func take(pair: (int, Array[int])) -> void:
		print("took ", pair, " ", pair[1].get_typed_builtin() == TYPE_INT)

	func take_rest(...pairs: Array[(int, Array[int])]) -> void:
		for pair in pairs:
			print("rest ", pair, " ", pair[1].get_typed_builtin() == TYPE_INT)


func identity[T](value: T) -> T:
	return value


func with_default(pair: (int, Array[int]) = (1, [])) -> void:
	print("default ", pair, " ", pair[1].get_typed_builtin() == TYPE_INT)


func test():
	var receiver := Receiver.new()
	receiver.take((1, []))
	receiver.take_rest((2, []), (3, [4]))
	with_default()
	var specialized := identity[(int, Array[int])]((5, []))
	print("specialized ", specialized, " ", specialized[1].get_typed_builtin() == TYPE_INT)
