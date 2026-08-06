func collect(prefix: String, ...values: Array[int]) -> long:
	return prefix.length() + values.size()

func callback() -> Callable[[String, ...Array[int]], long]:
	return collect
