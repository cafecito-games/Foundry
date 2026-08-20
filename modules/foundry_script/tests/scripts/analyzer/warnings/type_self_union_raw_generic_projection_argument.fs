# A value projected out of a raw generic receiver has a type, but one naming an erased parameter the
# call site cannot reify, so nothing about it is decided statically. The crossing is reported exactly
# as it is for a parameter that mentions no `Self`.
class Box[T]:
	var value: T


class Receiver:
	func take(_value: int | (int, Self)) -> void:
		pass

	func take_plain(_value: int | String) -> void:
		pass

	func drive(raw: Box) -> void:
		take(raw.value)
		take_plain(raw.value)


func test() -> void:
	pass
