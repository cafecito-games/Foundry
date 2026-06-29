# The handler's return value is coerced to each intercepted method's declared
# return type: passed through when already correct, converted for compatible
# builtins (int -> float), and ignored for `void`.
abstract class Service:
	abstract func get_count() -> int
	abstract func get_ratio() -> float
	abstract func describe() -> String
	abstract func do_nothing() -> void
	abstract func passthrough() -> Variant

func test() -> void:
	var box: Array = [null]
	var service: Object = create_proxy_dynamic(Service, func(_method: StringName, _args: Array) -> Variant:
		return box[0])

	box[0] = 5
	print(service.call("get_count"))

	box[0] = 2
	var ratio: Variant = service.call("get_ratio")
	print(ratio)
	print(typeof(ratio) == TYPE_FLOAT)

	box[0] = "hello"
	print(service.call("describe"))

	box[0] = 123
	print(service.call("do_nothing"))

	box[0] = Vector2(1, 2)
	print(service.call("passthrough"))
