# The handle spelling and the concrete spelling accept the same value and build the same case, so a
# generic constructor is neither weaker nor stricter than writing the class out.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func via_handle[T: Receiver](handle: Type[T], value: Variant) -> Variant:
	return handle.Message.Attach(1, value)


func via_concrete(value: Variant) -> Variant:
	return Sub.Message.Attach(1, value)


func test() -> void:
	var sub: Variant = Sub.new()
	var from_handle: Variant = via_handle(Sub, sub)
	var from_concrete: Variant = via_concrete(sub)
	print(from_handle is Receiver.Message.Attach)
	print(from_concrete is Receiver.Message.Attach)
	print(from_handle == from_concrete)
