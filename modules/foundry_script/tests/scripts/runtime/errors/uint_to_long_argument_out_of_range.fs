# The implicit `uint` -> `long` widening at an argument binding is decided by value: a gradual
# source carrying a `ulong` value above the `uint` range is rejected before the callee runs, even
# though the value would fit `long` — mirroring the static rule that `ulong` -> `long` requires an
# explicit cast.
func take(value: long) -> void:
	print(value)


func test() -> void:
	var v: Variant = 1099511627776UL
	print("before call")
	take(v)
	print("unreachable")
