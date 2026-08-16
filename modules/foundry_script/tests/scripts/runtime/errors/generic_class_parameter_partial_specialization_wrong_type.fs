# The half of the specialization the receiver does know is still enforced: `HalfPair` fixes `Pair`'s
# `K` to `int` even though its own `V` is unsupplied, so a `K` slot rejects a String.
class Pair[K, V]:
	func keep_key(value) -> K:
		var kept: K = value
		return kept


class HalfPair[V] extends Pair[int, V]:
	pass


func test() -> void:
	var half := HalfPair.new()
	print(half.keep_key("not an int"))
