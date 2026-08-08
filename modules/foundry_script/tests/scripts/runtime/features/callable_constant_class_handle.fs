class Base:
	static func spawn() -> int:
		return 7


class InstanceReceiver:
	func method() -> int:
		return 11


func test() -> void:
	var direct := Callable(Base, "spawn")
	var handle: Type[Base] = Base
	var indirect := Callable(handle, "spawn")
	print("direct object == Base: ", direct.get_object() == Base)
	print("direct method: ", direct.get_method())
	print("direct result: ", direct.call())
	print("direct equals indirect: ", direct == indirect)

	var missing := Callable(Base, "does_not_exist")
	print("missing method: ", missing.get_method())
	print("missing valid: ", missing.is_valid())
	print("empty is null: ", Callable().is_null())
	print("instance result: ", Callable(InstanceReceiver.new(), "method").call())

	print("Vector2 unchanged: ", Vector2(1, 2) == Vector2(1, 2))
	print("Color unchanged: ", Color(1, 0, 0) == Color(1, 0, 0))
	print("StringName unchanged: ", StringName("x") == &"x")
