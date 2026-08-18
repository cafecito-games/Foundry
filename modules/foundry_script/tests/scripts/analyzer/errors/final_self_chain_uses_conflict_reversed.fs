# The same conflict with the descendant declared before the ancestor. Chain coherence is a property of
# the declarations, not of the order they appear in, so the answer must not move.
trait FsrKeeper[T]:
	func label() -> String:
		return "keeper"


final class FsrSub extends FsrBase:
	uses FsrKeeper[FsrPair[int, Self]]


class FsrBase:
	uses FsrKeeper[FsrPair[int, String]]


class FsrPair[A, B]:
	pass


func test() -> void:
	print("unreachable")
