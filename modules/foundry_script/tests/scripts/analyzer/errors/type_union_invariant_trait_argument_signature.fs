# Trait conformance projects the arguments a chain fixed and compares them with the destination's,
# through the same union comparison. A conformance bound at a union whose callable alternative has a
# different signature therefore contradicts a destination bound at the other, instead of reading as
# the same binding.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Handler:
	uses Keeper[Callable[[String], void] | int]


func take(value: Keeper[Callable[[int], void] | int]) -> void:
	print(value.label())


func test() -> void:
	take(Handler.new())
