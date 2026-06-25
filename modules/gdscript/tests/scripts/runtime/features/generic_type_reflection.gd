class Box[T]:
	var value


class Pair[K, V: RefCounted]:
	var first
	var second


class Plain:
	var value


func _describe(script) -> void:
	@warning_ignore("unsafe_method_access")
	print(script.is_generic())
	@warning_ignore("unsafe_method_access")
	var params: Array = script.get_type_parameter_list()
	print(params.size())
	for param in params:
		var line := "%s index=%d scope=%s has_bound=%s" % [param["name"], param["index"], param["scope"], param["has_bound"]]
		if param["has_bound"]:
			line += " bound=%s" % param["bound"]["class_name"]
		print(line)


func test():
	_describe(Box)
	_describe(Pair)
	_describe(Plain)
