# A union slot emits no conversion, so a constant that would have to change carrier to reach an
# alternative cannot reach it, however well the value fits. The rule holds whether or not another
# alternative names `Self`: the signed constant below fits an unsigned alternative's range and is still
# refused, exactly as it is for a union that mentions no `Self` at all.
class Receiver:
	func take_self(_value: uint | (int, Self)) -> void:
		pass

	func take_plain(_value: uint | String) -> void:
		pass

	func drive() -> void:
		take_self(5)
		take_plain(5)


func test() -> void:
	pass
