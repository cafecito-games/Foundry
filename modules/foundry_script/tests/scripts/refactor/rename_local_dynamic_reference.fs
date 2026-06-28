extends Node

func compute() -> int:
	var local_value := 1
	get("local_value")
	return local_value
