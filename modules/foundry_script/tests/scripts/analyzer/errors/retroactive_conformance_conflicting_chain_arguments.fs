# A retroactive conformance is a second evidence source the runtime relation accepts on its own, so
# it may not record arguments a binding on the target's class chain already fixed: the value would
# otherwise be a `Keeper[int]` and a `Keeper[String]` at the same time.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class BaseKeeper:
	uses Keeper[String]


class ChildKeeper extends BaseKeeper:
	pass


extend ChildKeeper uses Keeper[int]:
	func label() -> String:
		return "child"
