# The `uint` -> `long` widening decides only whether the value may cross carriers; the destination's
# declared width is a separate question asked afterwards. 4294967295 crosses, then fails to fit the
# declared `int`, so the diagnostic names `int` rather than the crossing.
func test() -> void:
	var v: Variant = 4294967295U
	print("before store")
	var _i: int = v
	print("unreachable")
