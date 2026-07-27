# Widening only applies to the nullable form of the *same* type parameter. Two distinct parameters
# stay incompatible even when the target is nullable, so `T` cannot be stored in a `U?`.
class Pair[T, U]:
	var second: U?

	func store_first(value: T) -> void:
		second = value


func test():
	print(Pair)
