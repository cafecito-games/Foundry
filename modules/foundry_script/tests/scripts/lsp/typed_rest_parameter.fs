func collect(prefix: String, ...values: Array[int]) -> int:
	return values.size()

func gather(...args: Array) -> int:
	return args.size()

func use() -> void:
	collect("n", 1, 2, 3)
	gather(1, "two")
