# A method-scope type parameter bounded by a `final` class is as closed as a class-scope one, so an
# unresolved method on it is rejected the same way.
final class FrmMessage:
	func send() -> void:
		pass


func poke[T: FrmMessage](value: T) -> void:
	value.deliver()


func test() -> void:
	pass
