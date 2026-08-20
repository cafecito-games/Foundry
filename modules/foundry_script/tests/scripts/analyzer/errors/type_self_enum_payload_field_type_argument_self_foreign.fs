# The payload position an application filled with the caller's `Self` is not answerable by receiver
# identity: the receiver expression is a value of the union's class, not the calling frame's own
# receiver, so ordinary type equality rejects it. The declaration-written `Self` keeps the opposite
# owner, so swapping the two arguments rejects both.
class Receiver:
	enum Box[T]:
		Empty
		Full(value: T, owner: Self)

	func construct(other: Receiver) -> void:
		var foreign := other.Box[Self].Full(other, other)
		var swapped := other.Box[Self].Full(other, self)
		print(foreign, swapped)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
