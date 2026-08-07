# A static function reached through an instance of a specialized generic class resolves `Self`
# against that instance's class, carrying the arguments it was reified with, so the call dispatches
# exactly as `Box[int].hold(...)` would.
@warning_ignore_start("static_called_on_instance")


class Box[T]:
	var value: T

	static func hold(item: Self) -> Self:
		print("held item is a Box: %s" % [item is Box])
		return item


func test() -> void:
	var box := Box[int].new()
	var held := box.hold(Box[int].new())
	held.value = 7
	print("held value: %s" % [held.value])
