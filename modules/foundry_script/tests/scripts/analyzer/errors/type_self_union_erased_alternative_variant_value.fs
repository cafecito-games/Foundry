# An annotated `Variant` is a hard type, but it promises no more about the value than a weak type does,
# so it faces the same question a weak one faces: a destination holding an erased type parameter has no
# run-time type to check the value against, and the promise that the run time will check what the
# analyzer could not cannot be made. The carrier the value was written with does not change the answer,
# and neither does an alternative naming `Self`.
class Receiver:
	func take_self[T](_source: T) -> void:
		var dynamic: Variant = 5
		var link: Array[T] | (int, Self) = dynamic
		link = dynamic
		print(link)

	func take_plain[T](_source: T) -> void:
		var dynamic: Variant = 5
		var link: Array[T] | String = dynamic
		link = dynamic
		print(link)

	func make_self[T](_source: T) -> Array[T] | (int, Self):
		var dynamic: Variant = 5
		return dynamic

	func make_plain[T](_source: T) -> Array[T] | String:
		var dynamic: Variant = 5
		return dynamic


func test() -> void:
	pass
