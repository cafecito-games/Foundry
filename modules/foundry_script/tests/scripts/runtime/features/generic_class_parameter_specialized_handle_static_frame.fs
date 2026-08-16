# A static function has no receiver, so nothing can reify the parameter. The construction falls back
# to the unspecialized form rather than fabricating an argument, and it is not an error: the analyzer
# is what refuses to make a class parameter checkable in a static frame.
class Holder[T]:
	var value: T


class Wrapper[U]:
	static func build() -> Variant:
		return Holder[U].new()


func test() -> void:
	var built: Variant = Wrapper.build()
	print(built is Holder)
	print(built is Holder[int])
	built.value = 5
	print(built.value)
	print("static frame construction ok")
