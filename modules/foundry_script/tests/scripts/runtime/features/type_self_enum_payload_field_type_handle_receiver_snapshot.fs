# Evaluating the receiver handle before the arguments means the check uses the handle's *value* at
# that point. The handle here lives in a member, and an argument expression reassigns that member
# while the payload is still being built: the `Sub` value is nonetheless accepted, because the check
# reads the `Sub` handle the receiver evaluated to rather than the `Other` handle the member holds by
# the time the payload field is stored.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


class Other:
	extends Receiver


class Maker[T: Receiver]:
	var handle: Type[T]

	func swap(replacement: Type[T]) -> int:
		handle = replacement
		return 1

	func make(value: Variant) -> Variant:
		return handle.Message.Attach(swap(Other), value)


func test() -> void:
	var maker := Maker[Receiver].new()
	maker.handle = Sub
	var made: Variant = maker.make(Sub.new())
	print(made is Receiver.Message.Attach)
	# The reassignment really happened; it just cannot retarget a check the receiver already decided.
	print(maker.handle == Other)
