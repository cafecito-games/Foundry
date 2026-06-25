class Box[T]:
	var value

	func _init(v = null):
		value = v


func test():
	var int_box := Box[int].new(5)
	print(int_box.value)

	var string_box := Box[String].new("hi")
	print(string_box.value)

	var nested := Box[Array[int]].new([1, 2, 3])
	print(nested.value)
