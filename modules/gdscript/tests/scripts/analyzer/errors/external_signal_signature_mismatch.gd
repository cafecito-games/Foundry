const Provider = preload("external_signal_signature_mismatch_provider.notest.gd")

func test() -> void:
	var s: Signal[[String]] = Provider.new().get_signal()
	print(s)
