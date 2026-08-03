# A signal inherited from a generic base is specialized against the receiver's type arguments, so
# every statically resolved `emit()` is validated against the concrete parameter type rather than
# the declaration-site type parameter `T`.
class Base[T]:
	signal reported(value: T)


class Child extends Base[int]:
	func go() -> void:
		reported.emit("nope")
		self.reported.emit("nope")


func test(child: Child, base: Base[int]) -> void:
	child.reported.emit("nope")
	base.reported.emit("nope")
