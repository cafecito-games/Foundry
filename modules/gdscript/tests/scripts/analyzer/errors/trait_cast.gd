extends RefCounted

trait Damageable:
	pass

func test(value: Variant) -> void:
	var _result = value as Damageable
