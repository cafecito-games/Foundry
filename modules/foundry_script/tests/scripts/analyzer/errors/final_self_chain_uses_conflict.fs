# `FscSub` is final and non-generic, so the `Self` it wrote inside `FscPair[int, Self]` denotes
# `FscSub`. The binding it publishes is therefore `FscKeeper[FscPair[int, FscSub]]`, which contradicts
# the `FscKeeper[FscPair[int, String]]` its base already fixed for the whole chain. The applied vector
# is reified before the chain comparison, so the conflict is seen and the diagnostic renders the
# reified form rather than the `Self` the author typed.
trait FscKeeper[T]:
	func label() -> String:
		return "keeper"


class FscPair[A, B]:
	pass


class FscBase:
	uses FscKeeper[FscPair[int, String]]


final class FscSub extends FscBase:
	uses FscKeeper[FscPair[int, Self]]


func test() -> void:
	print("unreachable")
