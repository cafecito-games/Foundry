# An unresolved method on a `final` receiver is a hard error only when nothing can ever supply it.
# Instance witnesses are deliberately not resolved statically, so this call is not typed from the
# conformance — but the conformance is reachable, the runtime dispatches to it, and analysis must
# leave the call alone (merely unsafe) instead of rejecting a program that works.
const _Conformance = preload("frw_conformance.notest.fs")


func probe() -> String:
	var widget := FrwWidget.new()
	return widget.frw_gadget()
