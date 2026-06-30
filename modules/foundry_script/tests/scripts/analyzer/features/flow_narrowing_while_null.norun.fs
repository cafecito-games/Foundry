extends RefCounted

func test(value: RefCounted?) -> void:
	while value != null:
		value.get_instance_id()
		break
