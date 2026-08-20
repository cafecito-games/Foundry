# A proxy's handler return crosses into a union slot like any other value, so it answers the same
# membership question -- retyping an untyped container into an alternative whose declared element types
# its contents already satisfy. Without that a proxy would reject the very value the plain
# union-returning function beside it accepts, and substitute the slot's default instead.
abstract class Service:
	abstract func fetch() -> Array[int] | String


func plain(value: Variant) -> Array[int] | String:
	return value


func test() -> void:
	var box: Array = [null]
	var service: Object = create_proxy_dynamic(Service, func(_method: StringName, _args: Array) -> Variant:
		return box[0])

	box[0] = [1, 2]
	print("proxy retyped ", service.call("fetch"))
	print("plain retyped ", plain([1, 2]))

	box[0] = "text"
	print("proxy direct alternative ", service.call("fetch"))
	print("plain direct alternative ", plain("text"))
