# A native engine class (`RefCounted`) is retroactively conformed to `RtcNativePingable`, supplying
# `ping()` externally. Native objects have no FSInstance, so this exercises the native dispatch seam:
# the witness is dispatched on a plain Object through the conformance registry, keyed by the object's
# engine-class hierarchy. Covers dispatch through a trait-typed local, a trait-typed parameter, and a
# generic bound `T: RtcNativePingable`; `is`/`as` against the conformed trait (including through a
# widened `Object`); and the native inheritance walk, since a `Resource` (a `RefCounted` subclass)
# satisfies the conformance declared on the base class.
extend RefCounted uses RtcNativePingable:
	func ping() -> int:
		# `is_class` is a native `Object` method resolved against the target's engine surface and
		# dispatched on the bound native `self` at runtime, proving witness bodies can use the native
		# surface end-to-end (not just constants).
		if is_class("RefCounted"):
			return 21 * 2
		return 0


func via_param(p: RtcNativePingable) -> int:
	return p.ping()


func via_bound[T: RtcNativePingable](value: T) -> int:
	return value.ping()


func test() -> void:
	var r := RefCounted.new()
	var p: RtcNativePingable = r
	print(p.ping())
	print(via_param(r))
	print(via_bound(r))
	print(r is RtcNativePingable)
	var as_pingable := r as RtcNativePingable
	print(as_pingable.ping())
	var as_object: Object = r
	print(as_object is RtcNativePingable)
	var res := Resource.new()
	print(res is RtcNativePingable)
	print((res as RtcNativePingable).ping())
