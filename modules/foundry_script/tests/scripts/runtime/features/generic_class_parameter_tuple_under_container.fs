# A tuple nested under a typed container is deliberately not checked positionally. `Array[(int, T)]`
# is stored at run time as a typed array whose element type is a plain `Array` carrier -- the tuple
# shape is not expressible there -- so a store-time positional scan would enforce a constraint the
# container itself stops enforcing on the very next `push_back`. The slot therefore keeps accepting a
# wrong-shaped, wrong-arity element; only a tuple in a slot of its own carries the check.
class Crate[T]:
	func keep_all(value) -> Array[(int, T)]:
		var kept: Array[(int, T)] = value
		return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var crate := Crate[int].new()
	# Built untyped so the analyzer never sees the elements; each one only has to be an Array.
	var wrong = supply([(1, 2), ("wrong", "shape", "and arity")])
	print(crate.keep_all(wrong))
	print("tuple under container ok")
