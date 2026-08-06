# The carve-out that keeps the final-receiver rule true also covers a receiver typed as a type
# parameter bounded by that final class. The conformance is reachable and the runtime dispatches to
# it, so the call stays merely unsafe instead of being rejected.
const _Conformance = preload("frw_conformance.notest.fs")


class Holder[T: FrwWidget]:
	var value: T

	func probe() -> String:
		return value.frw_gadget()
