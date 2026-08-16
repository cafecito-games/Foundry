# A static function has no receiver, so nothing can reify the parameter. The construction falls back
# to the unspecialized form rather than fabricating an argument, and it is not an error: the analyzer
# is what refuses to make a class parameter checkable in a static frame.
class Holder[T]:
	var value: T


class Wrapper[U]:
	static func build() -> Variant:
		return Holder[U].new()

	# A lambda in a static frame has no receiver to capture either, so the construction inside it must
	# not ask for one: a self-lambda is exactly what a static call cannot create.
	static func build_in_lambda() -> Variant:
		var maker := func():
			return Holder[U].new()
		return maker.call()


func test() -> void:
	var built: Variant = Wrapper.build()
	print(built is Holder)
	print(built is Holder[int])
	built.value = 5
	print(built.value)

	var from_lambda: Variant = Wrapper.build_in_lambda()
	print(from_lambda is Holder)
	print(from_lambda is Holder[int])
	print("static frame construction ok")
