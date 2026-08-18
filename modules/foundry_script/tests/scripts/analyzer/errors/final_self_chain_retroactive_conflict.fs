# The retroactive form of the same rule. The conformance target is what a `Self` written in the
# conformance's trait arguments names, so `FsqSub` being final and non-generic makes the recorded
# binding `FsqKeeper[FsqPair[int, FsqSub]]` -- which its base already fixed as
# `FsqKeeper[FsqPair[int, String]]` for the whole chain.
trait FsqKeeper[T]:
	func label() -> String:
		return "keeper"


class FsqPair[A, B]:
	pass


class FsqBase:
	uses FsqKeeper[FsqPair[int, String]]


final class FsqSub extends FsqBase:
	pass


extend FsqSub uses FsqKeeper[FsqPair[int, Self]]:
	pass


func test() -> void:
	print("unreachable")
