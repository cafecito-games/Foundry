func test() -> void:
	var  indexed_sink :  Callable[[int,...Array[String]],bool]
	var sink:AsyncCallable[[...Array[int]],void]
	print(indexed_sink,sink)
