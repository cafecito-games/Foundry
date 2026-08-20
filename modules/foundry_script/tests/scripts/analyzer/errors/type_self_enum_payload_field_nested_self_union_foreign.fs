# Decomposing the open schema through a carrier restores the owner that normalization dropped and
# widens admission no further. A value matching neither alternative -- a wrong carrier, a wrong element
# shape, or a class that is neither frame's receiver -- is still rejected, nested and at top level, and
# each diagnostic names a real type on both sides.
class Foreign:
	pass


class Receiver:
	enum Nested[T]:
		Empty
		DeclarationFirst(value: ((int, Self) | (int, T), int))
		ArgumentFirst(value: ((int, T) | (int, Self), int))

	enum Flat[T]:
		Empty
		DeclarationFirst(value: (int, Self) | (int, T))

	func construct(other: Receiver, foreign: Foreign) -> void:
		var nested_scalar := other.Nested[Self].DeclarationFirst(1)
		var nested_carrier := other.Nested[Self].DeclarationFirst(((1, self), 10, 100))
		var nested_element := other.Nested[Self].DeclarationFirst(((2, foreign), 20))
		var nested_shape := other.Nested[Self].ArgumentFirst((("text", self), 30))
		var nested_foreign := other.Nested[Self].ArgumentFirst(((3, foreign), 40))
		var flat_foreign := other.Flat[Self].DeclarationFirst((4, foreign))
		print(nested_scalar, nested_carrier, nested_element, nested_shape, nested_foreign, flat_foreign)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new(), Foreign.new())
