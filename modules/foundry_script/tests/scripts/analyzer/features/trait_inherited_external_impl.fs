extends "trait_inherited_external_impl_base.notest.fs"
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func test() -> void:
	pass
