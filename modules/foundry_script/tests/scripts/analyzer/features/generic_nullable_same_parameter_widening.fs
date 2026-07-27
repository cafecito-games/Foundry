# A non-nullable type parameter value widens to the nullable form of the same parameter. `T` -> `T?`
# can never introduce an unexpected null, so it is accepted statically on every surface (call
# arguments, initializers, returns, and assignments) without a runtime check or an
# UNSAFE_CALL_ARGUMENT warning, exactly like `Node` -> `Node?` for concrete types.
class Holder[T]:
	var stored: T?

	func _init(p_value: T?) -> void:
		stored = p_value

	static func of(value: T) -> Holder[T]:
		return Holder[T].new(value)

	func widen_local(value: T) -> T?:
		var widened: T? = value
		return widened

	func widen_return(value: T) -> T?:
		return value

	func store(value: T) -> void:
		stored = value


func test() -> void:
	var container: Holder[int] = Holder[int].of(7)
	print(container.stored)
	print(container.widen_local(3))
	print(container.widen_return(4))
	container.store(9)
	print(container.stored)
	print("generic nullable same parameter widening ok")
