const Provider = preload("external_callable_signature_nullable_param_provider.notest.gd")

func test() -> void:
	var handler: Callable[[Node], void] = Provider.new().get_cb()
	print(handler)
