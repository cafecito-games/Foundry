# foundry.reflection.get_type_arguments(instance) exposes the reified generic type arguments bound
# onto a FoundryScript instance (the `int` in `Box[int].new()`), as container-type descriptors. A
# non-generic or unspecialized instance reports no arguments.
class Box[T]:
	var value: T


class Pair[K, V]:
	var first: K
	var second: V


func test() -> void:
	var int_box := Box[int].new()
	var args: Array = foundry.reflection.get_type_arguments(int_box)
	print(args.size())
	print(args[0]["type"] == TYPE_INT)

	var pair := Pair[String, int].new()
	var pair_args: Array = foundry.reflection.get_type_arguments(pair)
	print(pair_args.size())
	print(pair_args[0]["type"] == TYPE_STRING)
	print(pair_args[1]["type"] == TYPE_INT)

	# A non-generic instance has no reified type arguments.
	print(foundry.reflection.get_type_arguments(RefCounted.new()).size())
	print("reflection type arguments ok")
