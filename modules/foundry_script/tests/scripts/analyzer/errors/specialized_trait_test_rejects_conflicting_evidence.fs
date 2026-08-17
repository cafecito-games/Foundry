# `is` and `as` ask the membership question the store asks, so a statically known conformer whose
# trait arguments contradict the tested type is a static error, exactly as the class spelling is.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


func test() -> void:
	var conformer := ForwardingKeeper[String].new()
	print(conformer is Keeper[int])
	print(conformer as Keeper[int])
