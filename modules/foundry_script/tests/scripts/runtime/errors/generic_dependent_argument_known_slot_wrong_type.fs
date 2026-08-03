# The unresolved `U` in `extends Base[Pair[int, U]]` must not erase the sibling `int` slot: a
# `Pair[float, String]` is rejected even against a raw `Mid`, whose second argument is unknown.
class Pair[A, B]:
	var first
	var second

	func _init(a = null, b = null):
		first = a
		second = b


class Base[X]:
	var value: X


class Mid[U] extends Base[Pair[int, U]]:
	pass


func test() -> void:
	var mid := Mid.new()
	var dynamic: Variant = mid
	dynamic.value = Pair[float, String].new(1.0, "one")
	print("unreached")
