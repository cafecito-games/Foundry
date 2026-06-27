class_name TypeSelfMethodInfo

func clone() -> Self:
	return self

func copy(other: Self) -> void:
	print(other is TypeSelfMethodInfo)


func test() -> void:
	for method in get_method_list():
		if method.name == "clone" or method.name == "copy":
			print(Utils.get_method_signature(method))
