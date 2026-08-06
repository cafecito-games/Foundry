# A cross-script trait witness is matched through the compiled `MethodInfo`/`PropertyInfo` boundary,
# which transports a scalar integer parameter's carrier only, not its declared width (see the width-
# erasure boundary documented at `FSAnalyzer::type_from_property()`). The base script's `amount: int`
# parameter therefore decodes wide at the witness-matching site even though both sides declare the
# same 32-bit `int`, so this now correctly reports a mismatch. Recovering the width across this
# boundary is tracked separately; this fixture pins the current, honest behavior rather than papering
# over the gap.
extends "trait_inherited_external_impl_base.notest.fs"
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func test() -> void:
	pass
