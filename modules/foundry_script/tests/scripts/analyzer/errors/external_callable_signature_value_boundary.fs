const Provider = preload("external_callable_signature_value_boundary_provider.notest.gd")

func test() -> void:
	var handler: Callable[[Callable[[String], void]], void] = Provider.new().get_cb()
	print(handler)
