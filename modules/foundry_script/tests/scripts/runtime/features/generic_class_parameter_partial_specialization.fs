# A receiver can reify some of a base's parameters and leave others open: `HalfPair` fixes `Pair`'s
# `K` to `int` through `extends` while its own `V` stays unsupplied on a raw instance. Evidence is
# tracked per parameter, so the slot that names the known one keeps validating instead of the whole
# check being dropped because a sibling parameter is unknown.
class Pair[K, V]:
	func keep_key(value) -> K:
		var kept: K = value
		return kept

	func keep_value(value) -> V:
		var kept: V = value
		return kept


class HalfPair[V] extends Pair[int, V]:
	pass


func test() -> void:
	var half := HalfPair.new()
	print(half.keep_key(5))
	# `V` has no reified argument on this receiver, so that slot stays gradual.
	print(half.keep_value("anything"))
	print("partial specialization ok")
