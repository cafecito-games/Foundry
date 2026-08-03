# A typed `Array[Pair]` never states the arguments the destination knows, so comparing element
# metadata proves nothing about them: the elements themselves still have to be checked.
class Pair[A, B]:
	var first
	var second

	func _init(a = null, b = null):
		first = a
		second = b


class Base[X]:
	var value: X


class Mid[U] extends Base[Array[Pair[int, U]]]:
	pass


func test() -> void:
	var mid := Mid.new()
	var dynamic: Variant = mid

	var accepted: Array[Pair] = [Pair[int, String].new(1, "one")]
	dynamic.value = accepted
	print(mid.value.size())

	var rejected: Array[Pair] = [Pair[float, String].new(1.0, "one")]
	dynamic.value = rejected
	print("unreached")
