# A same-carrier gradual store is checked too: a `ulong` value copied verbatim into a `uint` slot
# would leave the destination holding a magnitude its own declared width cannot represent.
func test() -> void:
	var v: Variant = 5000000000UL
	print("before store")
	var _u: uint = v
	print("unreachable")
