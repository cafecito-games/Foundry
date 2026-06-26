# A subclass whose base specialization is a composite that still mentions an open parameter
# (`Derived[T] extends Box[Array[T]]`) cannot be projected soundly: the fixed argument is erased at
# compile time, so the runtime carries no concrete evidence for it. Such a value is accepted under
# gradual typing rather than falsely rejected from an `Array[Box[Array[int]]]`.
class Box[T]:
	var value: T

	func _init(v = null):
		value = v


class Derived[T] extends Box[Array[T]]:
	pass


func make_derived() -> Variant:
	return Derived[int].new([1, 2])


func test() -> void:
	var boxes: Array[Box[Array[int]]] = []
	@warning_ignore("unsafe_call_argument")
	boxes.append(make_derived())
	print(boxes.size())
	print("typed array element dependent base projection ok")
