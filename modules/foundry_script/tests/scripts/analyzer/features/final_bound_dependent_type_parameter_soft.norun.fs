# A parameter bounded by another parameter keeps the soft treatment. Member resolution substitutes a
# bound exactly one step, so `U` is resolved against `T` and never against `FrmMessage`: even the
# valid `send()` call below is unresolved today. Rejecting these would reject working programs, so
# only a directly final-bounded parameter is closed.
final class FrmMessage:
	func send() -> void:
		pass


class Box[T: FrmMessage, U: T]:
	var value: U

	func poke() -> void:
		value.deliver()

	func nudge() -> void:
		value.send()
