# A value with no static type proves nothing, so no `Self` position can be checked against it. An
# alternative that names no `Self` is answered by ordinary compatibility, which admits such a value and
# books the crossing rather than refusing it -- the answer the whole annotation had before any
# alternative mentioned `Self`. A declared variable, an assignment target, and a return type all agree.
class Receiver:
	var counter = 5

	func make() -> int | (int, Self):
		var soft = counter
		return soft

	func drive() -> void:
		var soft = counter
		var link: int | (int, Self) = soft
		print("initialized ", link)
		link = soft
		print("assigned ", link)
		print("returned ", make())


func test() -> void:
	Receiver.new().drive()
