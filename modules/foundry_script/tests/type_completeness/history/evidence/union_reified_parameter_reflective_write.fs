class Keeper[T] extends RefCounted:
	var slot: Array[T] | int = 0

func test() -> void:
	var k = Keeper[int].new()
	var strings: Variant = ["a", "b"]
	k.set("slot", strings)
	print(k.slot)
