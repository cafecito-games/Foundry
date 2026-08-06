# Nullability decides whether the value may be null, not whether a subtype could supply the method,
# so a nullable final bound closes the receiver just as an exact one does. The reported bound drops
# the nullability, because it is the class that is final.
final class FrmMessage:
	func send() -> void:
		pass


class Box[T: FrmMessage?]:
	var value: T

	func poke() -> void:
		value.deliver()


func test() -> void:
	pass
