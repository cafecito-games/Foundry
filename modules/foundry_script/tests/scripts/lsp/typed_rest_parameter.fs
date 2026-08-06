func collect(prefix: String, ...values: Array[int]) -> long:
	return values.size()

func gather(...args: Array) -> long:
	return args.size()

func use() -> void:
	collect("n", 1, 2, 3)
	gather(1, "two")
