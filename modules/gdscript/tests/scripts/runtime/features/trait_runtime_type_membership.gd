extends RefCounted
uses Trackable

trait Damageable:
	pass

trait Trackable uses Damageable:
	pass

func accepts_damageable(_value: Damageable) -> bool:
	return true

func test() -> void:
	var value: Variant = self
	print(value is Damageable)
	print(value is Trackable)
	print((value as Damageable) != null)
	print(accepts_damageable(self))
