# A type parameter bounded by a `final` class reaches the same conformance its bound does. The
# reachable instance witness now types the call, so it neither errors as a closed-final bound nor
# degrades to an unsafe-access warning.
const _Conformance = preload("frw_conformance.notest.fs")


class Holder[T: FrwWidget]:
	var value: T

	func probe() -> String:
		return value.frw_gadget()
