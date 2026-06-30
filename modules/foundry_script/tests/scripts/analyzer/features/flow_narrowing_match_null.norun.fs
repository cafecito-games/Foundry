extends RefCounted

func test(value: RefCounted?) -> void:
	match value:
		null:
			pass
		_:
			value.get_instance_id()
