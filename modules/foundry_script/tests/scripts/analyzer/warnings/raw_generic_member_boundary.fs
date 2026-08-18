# A generic class named without type arguments stays legal: nothing here warns for holding, casting,
# or declaring a raw `Box`. What warns is a member whose type still names a parameter the receiver
# never bound, at the moment that value crosses into a typed slot.
class Box[T]:
	var stored: T

	func get_value() -> T:
		return stored

	func set_value(value: T) -> void:
		stored = value

	func size() -> int:
		return 1

	func get_pair() -> Pair[T, String]:
		return Pair.new()


class Pair[A, B]:
	var first: A
	var second: B


class IntBox extends Box[int]:
	pass


func want_int(value: int) -> void:
	print(value)


func want_string(value: String) -> void:
	print(value)


# We don't want to execute it because of errors, just analyze.
func no_exec_raw_receiver(box: Box) -> void:
	want_int(box.get_value())
	var _local: int = box.get_value()
	box.set_value(5)
	want_int(box.size()) # No warning: `size()` names no parameter.
	var _same: Variant = box.get_value() # No warning: a Variant slot states nothing.


func no_exec_raw_return(box: Box) -> int:
	return box.get_value()


func no_exec_raw_nested(box: Box) -> void:
	want_int(box.get_pair().first)
	want_string(box.get_pair().second) # No warning: the concrete sibling is still concrete.


func no_exec_specialized(box: Box[int]) -> void:
	want_int(box.get_value()) # No warning.
	box.set_value(5) # No warning.
	var _local: int = box.get_value() # No warning.


func no_exec_inherited(box: IntBox) -> void:
	want_int(box.get_value()) # No warning: the arguments were recovered through inheritance.
	box.set_value(5) # No warning.


func no_exec_declarations() -> void:
	var _raw: Box = Box.new() # No warning: holding a raw value is not a boundary.
	var _elements: Array[Box] = [] # No warning: a raw element type is not a boundary.


func test():
	pass
