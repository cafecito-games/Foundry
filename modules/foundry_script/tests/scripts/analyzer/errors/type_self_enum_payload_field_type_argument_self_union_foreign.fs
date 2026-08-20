# Asking a union payload field's alternatives individually restores the owner that normalization
# dropped; it widens admission no further than that. A value matching neither alternative -- a wrong
# carrier, a wrong element shape, or a class that is neither frame's receiver -- is still rejected.
class Foreign:
	pass


class Receiver:
	enum Box[T]:
		Empty
		DeclarationFirst(value: (int, Self) | (int, T))
		ArgumentFirst(value: (int, T) | (int, Self))

	func construct(other: Receiver, foreign: Foreign) -> void:
		var unrelated_scalar := other.Box[Self].DeclarationFirst(1)
		var unrelated_element := other.Box[Self].DeclarationFirst((1, foreign))
		var unrelated_shape := other.Box[Self].ArgumentFirst(("text", self))
		var unrelated_foreign := other.Box[Self].ArgumentFirst((1, foreign))
		print(unrelated_scalar, unrelated_element, unrelated_shape, unrelated_foreign)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new(), Foreign.new())
