# A trait destination is invariant in its type arguments exactly like a class destination. The
# conformer's chain binds `Keeper`'s parameter to `String`, which contradicts the declaration, so the
# store is rejected statically instead of landing a `Keeper[String]` in a `Keeper[int]` slot.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


func test() -> void:
	var conformer := ForwardingKeeper[String].new()
	var slot: Keeper[int] = conformer
	print(slot)
