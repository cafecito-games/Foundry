trait SuppliesSelf:
	abstract func items() -> Array[Self]
	abstract func mapping() -> Dictionary[String, Self]


abstract class Holder:
	uses SuppliesSelf


func method_signature(target: Object, method_name: StringName) -> String:
	for method in target.get_method_list():
		if method.name == method_name:
			return Utils.get_method_signature(method)
	return "<missing>"


func test() -> void:
	var proxy := foundry.reflection.create_proxy_dynamic(Holder, func(_method_name: StringName, _args: Array) -> Variant:
		return null
	) as Holder
	print(method_signature(proxy, &"items"))
	print(method_signature(proxy, &"mapping"))
