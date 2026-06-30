# A foreign class (`Gadget`) is retroactively conformed to the `Pingable` trait, supplying the
# required `ping()` witness externally. The witness reads the target's own `power` member, so it must
# be compiled against `Gadget`'s layout and dispatched with the `Gadget` instance as `self`. This
# fixture proves the witness actually runs end-to-end when the value is used through the trait type:
# via a trait-typed local, a trait-typed parameter, and a generic bound `T: Pingable`.
extend Gadget uses Pingable:
	func ping() -> int:
		return power * 2


func via_param(p: Pingable) -> int:
	return p.ping()


func via_bound[T: Pingable](value: T) -> int:
	return value.ping()


func test() -> void:
	var g := Gadget.new()
	var p: Pingable = g
	print(p.ping())
	print(via_param(g))
	print(via_bound(g))
