const Provider = preload("external_signal_member_signature_mismatch_provider.notest.gd")

func test() -> void:
	var s: Signal[[String]] = Provider.new().pinged
	print(s)
