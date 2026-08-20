# The handle expression a reified payload check reads is evaluated exactly once, before the
# arguments -- the order the construction is written in -- so a side-effectful receiver runs the same
# number of times it would in any other call.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


class Counter:
	static var reads := 0


func pick[T: Receiver](handle: Type[T]) -> Type[T]:
	Counter.reads += 1
	return handle


func construct[T: Receiver](handle: Type[T], value: Variant) -> Variant:
	return pick(handle).Message.Attach(1, value)


func test() -> void:
	var sub: Variant = Sub.new()
	print(construct(Sub, sub) is Receiver.Message.Attach)
	print(Counter.reads)
