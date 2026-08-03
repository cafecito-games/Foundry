# The direct member-store opcode goes through the same binding validator as a dynamic property write,
# so a known-but-partial slot rejects identically from inside the class.
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


func test() -> void:
	var mid := Mid.new()
	mid.store(Pair[float, String].new(1.0, "one"))
	print("store rejected")
