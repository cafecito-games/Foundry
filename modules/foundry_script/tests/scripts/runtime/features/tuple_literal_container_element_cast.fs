# A cast target types the tuple literal it casts.
func test():
	var pair := (1, []) as (int, Array[int])
	print(pair)
	print(pair[1].get_typed_builtin() == TYPE_INT)
