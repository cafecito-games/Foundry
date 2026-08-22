signal reported(value: int)

func _on_reported(value) -> void:
	print("got ", value, " type ", typeof(value))

func test() -> void:
	reported.connect(_on_reported)
	var loose: Variant = "not an int"
	reported.emit(loose)
