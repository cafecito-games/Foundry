# A specialization's type argument is matched invariantly, so a union standing there is one type rather
# than a choice of them. Decomposing the open schema through that carrier recovers the `Self` owner a
# collapse drops and nothing else: a union whose members stay distinct once the application binds them
# is never distributed over the constructor, so neither a single alternative nor an unrelated
# specialization is admitted.
class Keeper[E]:
	var value: E

	func _init(initial: E) -> void:
		value = initial


class Receiver:
	enum Box[T]:
		Empty
		Full(value: Keeper[String | (T, Self)])

	func construct(other: Receiver) -> void:
		var narrow_first := other.Box[int].Full(Keeper[String].new("text"))
		var narrow_second := other.Box[int].Full(Keeper[(int, Self)].new((1, self)))
		var unrelated := other.Box[int].Full(Keeper[int].new(2))
		print(narrow_first, narrow_second, unrelated)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
