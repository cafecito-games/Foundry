# The typed-return opcode applies the same value-checked `UINT` -> `INT` crossing as the typed
# assign: a gradual return of a `ulong` value above the `uint` range is a runtime error.
func produce() -> long:
	var v: Variant = 4294967296UL
	print("before return")
	return v


func test() -> void:
	# The error aborts the callee's frame; the caller observes the default value and continues.
	var _l: long = produce()
	print("after failed return")
