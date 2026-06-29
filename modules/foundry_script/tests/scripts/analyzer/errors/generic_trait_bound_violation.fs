# A type argument must use a trait that stands as its type parameter's bound; deriving
# from an unrelated type is not enough.
trait Damageable:
	abstract func take_damage(amount: int) -> void


class Sword:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass


class Plain:
	pass


class Box[T: Damageable]:
	var value: T


# `Plain` does not use `Damageable`, so it cannot satisfy the trait bound.
var bad: Box[Plain]


func test():
	print(bad)
