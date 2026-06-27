# A trait may stand as a generic type-parameter bound. A type argument satisfies the
# bound when it uses the trait, and inside the body a `T`-typed value exposes the
# trait's required members for static checking.
trait Damageable:
	abstract func take_damage(amount: int) -> void


class Sword:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass


class Shield:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass


# `BroadSword` does not use `Damageable` itself, but inherits the use from `Sword`.
class BroadSword extends Sword:
	pass


class Box[T: Damageable]:
	var value: T

	func hit() -> void:
		# `value` is typed `T`, bounded by the `Damageable` trait, so its members resolve.
		value.take_damage(5)


# `Sword` and `Shield` use `Damageable` directly; `BroadSword` satisfies the bound through
# its ancestor. A bound check resolved while building an inheriting class still sees the use.
var sword_box: Box[Sword]
var shield_box: Box[Shield]
var broadsword_box: Box[BroadSword]
# The trait itself satisfies its own bound, consistent with using it as any other type
# argument (e.g. `Array[Damageable]`): a `Damageable`-typed slot holds conforming values.
var trait_box: Box[Damageable]


class SwordBoxHolder extends Box[BroadSword]:
	pass


func test():
	print(sword_box)
	print(shield_box)
	print(broadsword_box)
	print(trait_box)
	print("generic trait bounds ok")
