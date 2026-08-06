# A type parameter bounded by a `final` class has exactly the bound's value set: no subtype can
# exist, so a method the bound does not declare can never be supplied. The call below used to take
# the soft "may be present on a subtype" path and fail only when the line ran.
final class FrmMessage:
	func send() -> void:
		pass


class Box[T: FrmMessage]:
	var value: T

	func poke() -> void:
		value.deliver()


func test() -> void:
	pass
