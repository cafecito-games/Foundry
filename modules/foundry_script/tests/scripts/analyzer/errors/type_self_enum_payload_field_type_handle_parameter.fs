# A `Type[T]` handle is an exact spelling: a payload `Self` field substitutes to the represented
# type parameter, so a `T`-typed value is admitted and anything else is rejected as not being `T`.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func construct_via_handle[T: Receiver](handle: Type[T], value: T, foreign: Receiver) -> void:
	var ok := handle.Message.Attach(1, value)
	var bad := handle.Message.Attach(2, foreign)
	print(ok, bad)


func test() -> void:
	pass
