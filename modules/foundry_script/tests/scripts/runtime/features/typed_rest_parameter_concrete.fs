class Marker:
	var id := 0

func collect(...values: Array[int]) -> int:
	Utils.check(values.is_typed())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	return values.size()

func collect_floats(...values: Array[float]) -> int:
	# Surplus arguments follow the fixed-parameter conversion policy, so passing ints stores floats.
	for value in values:
		Utils.check(typeof(value) == TYPE_FLOAT)
	return values.size()

func collect_nullable(...values: Array[Marker?]) -> int:
	# A nullable element cannot be expressed as a typed container, so the collected array is erased
	# exactly like an ordinary `Array[Marker?]` local.
	var ordinary: Array[Marker?] = []
	Utils.check(values.is_typed() == ordinary.is_typed())
	return values.size()

func collect_gradual(...values: Array) -> int:
	Utils.check(!values.is_typed())
	return values.size()

func test() -> void:
	Utils.check(collect() == 0)
	Utils.check(collect(1, 2, 3) == 3)
	Utils.check(collect_floats(1, 2) == 2)
	Utils.check(collect_nullable(Marker.new(), null) == 2)
	Utils.check(collect_gradual(1, "two") == 2)

	var callback := collect
	Utils.check(callback.call(4, 5) == 2)
	Utils.check(callback.callv([6, 7, 8]) == 3)
	print("ok")
