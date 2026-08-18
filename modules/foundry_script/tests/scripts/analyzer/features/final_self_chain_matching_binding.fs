# Reification is what makes the comparison meaningful in both directions: an ancestor that names the
# descendant explicitly states exactly what the descendant's reified `Self` states, so the direct and
# the retroactive form are both accepted.
trait FsmKeeper[T]:
	func label() -> String:
		return "keeper"


class FsmPair[A, B]:
	pass


class FsmBase:
	uses FsmKeeper[FsmPair[int, FsmSub]]


final class FsmSub extends FsmBase:
	uses FsmKeeper[FsmPair[int, Self]]


class FsmRetroBase:
	uses FsmKeeper[FsmPair[int, FsmRetroSub]]


final class FsmRetroSub extends FsmRetroBase:
	pass


extend FsmRetroSub uses FsmKeeper[FsmPair[int, Self]]:
	pass


func test() -> void:
	print(FsmSub.new().label())
	print(FsmRetroSub.new().label())
