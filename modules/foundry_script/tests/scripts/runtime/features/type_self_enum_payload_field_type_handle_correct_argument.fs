# Reifying a `Self` payload field against the handle only rejects values the handle's class does not
# admit: the exact class is accepted, and so is a subclass instance constructed through a base-class
# handle, since the handle names the class the value has to be an instance of, not its exact class.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func construct[T: Receiver](handle: Type[T], value: Variant) -> Variant:
	return handle.Message.Attach(1, value)


func test() -> void:
	var sub: Variant = Sub.new()
	print(construct(Sub, sub) is Receiver.Message.Attach)
	print(construct(Receiver, sub) is Receiver.Message.Attach)
