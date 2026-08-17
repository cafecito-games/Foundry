# Re-applying a trait a base already applied is legal as long as the arguments agree: nothing about
# the chain's binding changes, so no reference can reach a differently typed body.
trait Keeper[T]:
	func keep(value: T) -> T:
		return value


class BaseKeeper:
	uses Keeper[String]


class ChildKeeper extends BaseKeeper:
	uses Keeper[String]


func use_base(base: BaseKeeper) -> void:
	print(base.keep("hello"))


func test() -> void:
	use_base(ChildKeeper.new())
