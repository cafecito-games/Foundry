# Builtin value types (`int`, `String`, `Array`) are retroactively conformed to `RtcBuiltinPingable`.
# Dispatch exercises trait-typed locals/parameters, generic bounds, `is`/`as` (including through
# `Variant`), typed-array erasure (`Array[int]` through `extend Array`), and value semantics: Array
# mutations through witness `self` are visible to the caller; int mutations are not.
extend int uses RtcBuiltinPingable:
	func ping() -> int:
		return self + 10


extend String uses RtcBuiltinPingable:
	func ping() -> int:
		return self.length()


extend Array uses RtcBuiltinPingable:
	func ping() -> int:
		self.append(99)
		return self.size()


func via_param(p: RtcBuiltinPingable) -> int:
	return p.ping()


func via_bound[T: RtcBuiltinPingable](value: T) -> int:
	return value.ping()


func test() -> void:
	var n := 5
	var p: RtcBuiltinPingable = n
	print(p.ping())
	print(via_param(n))
	print(via_bound(n))
	print(n is RtcBuiltinPingable)
	var as_pingable := n as RtcBuiltinPingable
	print(as_pingable.ping())
	var as_variant: Variant = n
	print(as_variant is RtcBuiltinPingable)
	var s := "hi"
	var sp: RtcBuiltinPingable = s
	print(sp.ping())
	var a: Array[int] = [1, 2]
	var ap: RtcBuiltinPingable = a
	print(ap.ping())
	print(a.size())
	var before := n
	var np: RtcBuiltinPingable = before
	_ = np.ping()
	print(before)
