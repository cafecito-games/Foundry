# A `Self` payload field constructed through a `Type[T]` handle is checked at runtime against the
# class the handle holds. The method type parameter erases to Variant, so the schema names no class,
# but the handle value is the reification of exactly that parameter -- so a foreign base-class
# instance is rejected here just as it is under the concrete `Sub.Message.Attach` spelling.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func construct[T: Receiver](handle: Type[T], value: Variant) -> Variant:
	return handle.Message.Attach(1, value)


func test() -> void:
	var boxed: Variant = Receiver.new()
	print(construct(Sub, boxed) is Receiver.Message.Attach)
