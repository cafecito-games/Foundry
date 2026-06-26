const Provider = preload("external_callable_signature_roundtrip_provider.notest.gd")

func test() -> void:
	var handler: Callable[[Callable[[int], void]], void] = Provider.new().get_cb()
	print(handler.is_valid())
