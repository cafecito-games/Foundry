# Per-position ownership carries through an unnamed tuple: in a mixed field the argument-originated
# element admits the calling frame's `self` while the declaration-originated element admits only the
# receiver expression, and a `Self` written inside a tuple type argument is the caller's at whatever
# depth it sits.
class Receiver:
	enum Box[T]:
		Empty
		Pack(pair: (T, Self))
		Only(value: T)

	func construct(other: Receiver) -> void:
		var mixed := other.Box[Self].Pack((self, other))
		var swapped := other.Box[Self].Pack((other, self))
		var nested := other.Box[(int, Self)].Only((1, self))
		var nested_foreign := other.Box[(int, Self)].Only((1, other))
		print(mixed, swapped, nested, nested_foreign)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
