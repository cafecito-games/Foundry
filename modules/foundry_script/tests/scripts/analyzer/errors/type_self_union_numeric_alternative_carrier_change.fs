# A union slot is untyped at run time and emits no conversion instruction, so an alternative reachable
# only by changing the value's carrier would hold an unconverted value and fail its own type test. The
# rule holds whether or not another alternative names `Self`, and whether or not the alternatives that
# do not name `Self` are numerous enough to still read as a set.
class Receiver:
	func take(_value: float | (int, Self)) -> void:
		pass

	func take_plain(_value: float | String) -> void:
		pass

	func drive() -> void:
		take(5)
		take_plain(5)


func test() -> void:
	pass
