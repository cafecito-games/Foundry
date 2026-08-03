# Once a leaf supplies `U`, the whole `Pair[int, U]` is known, so a mismatch in the reified slot is
# rejected just like the concrete one.
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


class Leaf extends Mid[String]:
	pass


func test() -> void:
	var leaf := Leaf.new()
	var dynamic: Variant = leaf
	dynamic.value = Pair[int, Node].new(1, null)
	print("unreached")
