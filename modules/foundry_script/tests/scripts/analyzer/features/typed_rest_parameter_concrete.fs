class Marker:
	var id := 0

func ints(...values: Array[int]) -> long:
	return values.size()

func labelled(prefix: String, ...values: Array[String]) -> String:
	return prefix + str(values.size())

func markers(...values: Array[Marker]) -> long:
	return values.size()

func nullable_markers(...values: Array[Marker?]) -> long:
	return values.size()

func nested(...values: Array[Array[int]]) -> long:
	return values.size()

func gradual(...values: Array) -> long:
	return values.size()

func explicit_variant(...values: Array[Variant]) -> long:
	return values.size()

func test() -> void:
	print(ints())
	print(ints(1, 2, 3))
	print(labelled("count: "))
	print(labelled("count: ", "a", "b"))
	print(markers(Marker.new(), Marker.new()))
	print(nullable_markers(Marker.new(), null))
	print(nested([1], [2, 3]))
	print(gradual(1, "two", null))
	print(explicit_variant(1, "two", null))
