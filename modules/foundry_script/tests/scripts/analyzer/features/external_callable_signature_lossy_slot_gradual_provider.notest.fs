enum Kind { A, B }

func get_cb() -> Callable[[Kind], void]:
	return func(_kind: Kind) -> void:
		pass
