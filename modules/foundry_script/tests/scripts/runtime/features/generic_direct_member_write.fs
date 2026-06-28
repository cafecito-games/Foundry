# Writes to a `T`-typed member compiled as direct member stores (inside the generic class itself —
# regular assignment, compound assignment, and field initializers) are validated at runtime against
# the type argument reified onto the instance, mirroring the external `set()` path. Values that
# convert are accepted; an instance constructed without explicit arguments stays permissive.
class Box[T]:
	var value: T
	var doubled: T = 0

	func put(v) -> void:
		value = v

	func bump(by) -> void:
		value += by


func test() -> void:
	var int_box := Box[int].new()
	int_box.put(5)
	print(int_box.value)
	int_box.put(7.0) # converts to int
	print(int_box.value)
	int_box.bump(3)
	print(int_box.value)
	print(int_box.doubled) # field initializer validated and stored

	# A raw, un-parameterized Box carries no reified argument, so stores are permissive.
	var raw := Box.new()
	raw.put("anything")
	print(raw.value)
	print("direct member write ok")
