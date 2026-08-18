# The retroactive conflict with the two class declarations swapped. The rule reads the chain, not the
# file, so the answer does not move.
trait FsyKeeper[T]:
	func label() -> String:
		return "keeper"


final class FsySub extends FsyBase:
	pass


class FsyBase:
	uses FsyKeeper[FsyPair[int, String]]


class FsyPair[A, B]:
	pass


extend FsySub uses FsyKeeper[FsyPair[int, Self]]:
	pass


func test() -> void:
	print("unreachable")
