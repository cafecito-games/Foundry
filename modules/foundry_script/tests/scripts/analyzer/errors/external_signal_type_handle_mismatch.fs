# An external signal declaring a class-handle parameter is not assignable to an instance-typed
# signal annotation.
const Provider = preload("external_signal_type_handle_mismatch_provider.notest.fs")

func test() -> void:
	var spawned: Signal[[Node]] = Provider.new().get_signal()
	print(spawned)
