# `class Mid[U] extends Base[Pair[int, U]]` fixes the outer `Pair` and its concrete `int` argument for
# every subclass while `U` stays open. A leaf that supplies `U` resolves the whole type, and a raw
# `Mid` keeps enforcing the `int` even though its second argument is unknown.
class Pair[A, B]:
	var first
	var second

	func _init(a = null, b = null):
		first = a
		second = b


class Base[X]:
	var value: X

	func store(v) -> void:
		value = v


class Mid[U] extends Base[Pair[int, U]]:
	pass


class Leaf extends Mid[String]:
	pass


func test() -> void:
	var leaf := Leaf.new()
	var leaf_dynamic: Variant = leaf
	leaf_dynamic.value = Pair[int, String].new(1, "one")
	print(leaf.value.first)

	# The same slot reached through the direct member-store opcode agrees with the dynamic write.
	leaf.store(Pair[int, String].new(2, "two"))
	print(leaf.value.first)

	# A raw `Mid` still knows the outer `Pair` and the `int`, but not `U`, so any second argument goes.
	var mid := Mid.new()
	var mid_dynamic: Variant = mid
	mid_dynamic.value = Pair[int, String].new(3, "three")
	print(mid.value.first)
	mid_dynamic.value = Pair[int, Node].new(4, null)
	print(mid.value.first)
