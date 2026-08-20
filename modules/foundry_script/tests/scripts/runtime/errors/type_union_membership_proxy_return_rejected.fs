# The rejecting half of `runtime/features/type_union_membership_proxy_return.fs`: a handler value no
# alternative describes is refused and the declared slot's default is substituted, exactly as an
# invalid typed return is. Kept apart from the accepting fixture because the refusal prints.
abstract class Service:
	abstract func fetch() -> Array[int] | String


func test() -> void:
	var service: Object = create_proxy_dynamic(Service, func(_method: StringName, _args: Array) -> Variant:
		return {"a": 1})

	print("rejected -> ", service.call("fetch"))
