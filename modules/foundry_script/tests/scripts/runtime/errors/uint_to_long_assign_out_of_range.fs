# A gradual store of a `ulong` value above the `uint` range into a `long` variable is a runtime
# error, never a silent null: the typed-assign opcode's `UINT` -> `INT` crossing is value-checked in
# every build configuration.
func test() -> void:
	var v: Variant = 18446744073709551615UL
	print("before store")
	var _l: long = v
	print("unreachable")
