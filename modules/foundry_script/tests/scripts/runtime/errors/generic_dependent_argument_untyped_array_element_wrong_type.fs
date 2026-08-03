# A partially known container has no element metadata to compare against when the assigned array is
# untyped, so the known evidence is enforced value by value instead of being waved through.
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

	var accepted: Array = [Pair[int, String].new(1, "one")]
	dynamic.value = accepted
	print(mid.value.size())

	var rejected: Array = [Pair[float, String].new(1.0, "one")]
	dynamic.value = rejected
	print("unreached")
