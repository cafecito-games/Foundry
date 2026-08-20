# Whether two alternatives collapse is asked of strict identity, not of the comparison a union's member
# vector uses: that one stops at a type's kind and carrier, where two callables read as one type
# whatever they accept. Alternatives that differ in a callable signature are genuinely distinct, so an
# invariant type argument is never split on them and a specialization naming one signature is not
# admitted for a field declared with the other.
type IntSink = Callable[[int], void]
type TextSink = Callable[[String], void]


class Keeper[E]:
	var value: E

	func _init(initial: E) -> void:
		value = initial


class Receiver:
	enum Box[T]:
		Empty
		Wide(value: Keeper[(TextSink, Self) | (IntSink, T)])

	func take_int(amount: int) -> void:
		print(amount)

	func construct(other: Receiver) -> void:
		var int_keeper := Keeper[(IntSink, Self)].new((take_int, self))
		var from_int := other.Box[Self].Wide(int_keeper)
		var unrelated := other.Box[Self].Wide(Keeper[int].new(1))
		print(from_int, unrelated)


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
