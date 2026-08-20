# The same reification covers a payload field the application itself typed by the handle's parameter:
# `handle.Box[T].Full(value)` fills `value: U` with `T`, which erases to Variant, so the handle value
# is again the only evidence of the class the field stands for.
class Receiver:
	enum Box[U]:
		Empty
		Full(value: U)


class Sub:
	extends Receiver


func construct[T: Receiver](handle: Type[T], value: Variant) -> Variant:
	return handle.Box[T].Full(value)


func test() -> void:
	var boxed: Variant = Receiver.new()
	print(construct(Sub, boxed) is Receiver.Box[Receiver].Full)
