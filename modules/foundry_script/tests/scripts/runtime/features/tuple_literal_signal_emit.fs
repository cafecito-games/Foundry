# A signal parameter's declared tuple type types a literal emitted through it.
signal fired(pair: (int, Array[int]))


func _on_fired(pair: (int, Array[int])) -> void:
	print("received ", pair, " ", pair[1].get_typed_builtin() == TYPE_INT)


func test():
	var connected := fired.connect(_on_fired)
	print(connected == OK)
	fired.emit((1, []))
