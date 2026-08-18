# Coherence covers the whole implied identity closure, so a `Self` reified inside a supertrait's
# argument is compared against the binding the chain fixed for that supertrait rather than for the
# trait the declaration names.
trait FssKeeper[T]:
	func label() -> String:
		return "keeper"


trait FssStoring[T] uses FssKeeper[T]:
	func store_label() -> String:
		return "storing"


class FssPair[A, B]:
	pass


class FssBase:
	uses FssKeeper[FssPair[int, String]]


final class FssSub extends FssBase:
	uses FssStoring[FssPair[int, Self]]


func test() -> void:
	print("unreachable")
