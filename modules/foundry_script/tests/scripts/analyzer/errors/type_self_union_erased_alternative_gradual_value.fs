# Booking a gradual crossing is a promise that the run time will check what the analyzer could not. A
# destination holding an erased type parameter has no run-time type to check against, so the promise
# cannot be made and the value is refused -- with the reason the reader can act on, and whether or not
# one of the alternatives names `Self`.
class Receiver:
	var counter = 5

	func take_self[T](_source: T) -> void:
		var soft = counter
		var link: Array[T] | (int, Self) = soft
		link = soft
		print(link)

	func take_plain[T](_source: T) -> void:
		var soft = counter
		var link: Array[T] | String = soft
		link = soft
		print(link)

	func make_self[T](_source: T) -> Array[T] | (int, Self):
		var soft = counter
		return soft

	func make_plain[T](_source: T) -> Array[T] | String:
		var soft = counter
		return soft


func test() -> void:
	pass
