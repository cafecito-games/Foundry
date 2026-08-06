# A nullable type argument only costs its own slot its runtime evidence. `extends Base[Pair[int, U?]]`
# still knows the outer `Pair` and the `int`, so a `Pair[float, ...]` is rejected.
class Pair[A, B]:
	var first
	var second

	func _init(a = null, b = null):
		first = a
		second = b


class Base[X]:
	var value: X


class Mid[U] extends Base[Pair[int, U?]]:
	pass


func test() -> void:
	var mid := Mid.new()
	var dynamic: Variant = mid
	dynamic.value = Pair[float, String].new(1.0, "one")
	print("unreached")
