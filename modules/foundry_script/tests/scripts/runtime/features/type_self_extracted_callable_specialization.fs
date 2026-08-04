# A callable extracted from a specialized generic handle keeps the concrete type arguments when it is
# invoked later, including when the handle that produced it was a transient value inside a scope that
# has already ended. The specialization is read back off the constructed object rather than off the
# call's declared return type, and the final assignment shows it is enforced, not merely recorded.
class Crate[T]:
	var value: T

	static func spawn() -> Self:
		return Self.new()

	static func extract() -> Callable:
		return spawn


func hold() -> Callable:
	return Crate[ImageTexture].extract()


func test() -> void:
	var direct: Callable = Crate[ImageTexture].spawn
	var made: Variant = direct.call()
	made.value = ImageTexture.new()
	print(made.value.get_class())

	var escaped := hold()
	var later: Variant = escaped.call()
	later.value = ImageTexture.new()
	print(later.value.get_class())

	later.value = Material.new()
