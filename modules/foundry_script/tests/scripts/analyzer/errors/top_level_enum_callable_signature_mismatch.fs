const Provider = preload("top_level_enum_callable_signature_provider.notest.gd")

func test() -> void:
	var handler: Callable[[String], void] = Provider.new().get_cb()
	print(handler)
