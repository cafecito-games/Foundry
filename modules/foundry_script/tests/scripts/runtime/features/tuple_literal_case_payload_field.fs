# A tagged-union case field typed as a tuple types the literal written for it.
enum Holder:
	Pair(pair: (int, Array[int]))
	Empty


func test():
	var held := Holder.Pair((1, []))
	match held:
		Holder.Pair(pair):
			print(pair, " ", pair[1].get_typed_builtin() == TYPE_INT)
		Holder.Empty:
			print("empty")
