# A trait may stand as a generic type-parameter bound. A type argument satisfies the
# bound when it uses the trait, and inside the body a `T`-typed value exposes the
# trait's required members for static checking.
trait Damageable:
	@abstract func take_damage(amount: int) -> void


class Sword:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass


class Shield:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass


class Box[T: Damageable]:
	var value: T

	func hit() -> void:
		# `value` is typed `T`, bounded by the `Damageable` trait, so its members resolve.
		value.take_damage(5)


# `Sword` and `Shield` both use `Damageable`, so both satisfy the trait bound.
var sword_box: Box[Sword]
var shield_box: Box[Shield]


func test():
	print(sword_box)
	print(shield_box)
	print("generic trait bounds ok")
