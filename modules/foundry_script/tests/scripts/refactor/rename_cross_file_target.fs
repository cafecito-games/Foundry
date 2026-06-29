extends RefCounted

var shared_count := 0

func bump() -> void:
	shared_count += 1

func read() -> int:
	return shared_count
