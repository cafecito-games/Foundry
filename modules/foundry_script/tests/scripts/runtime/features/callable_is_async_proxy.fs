# Callable.is_async() resolves the async flag of trait-proxy contract methods.
# These are dispatched dynamically by the proxy and are not present in the proxy
# script's compiled member functions, so the accessor reads them from the
# instance's method list (which surfaces the abstract trait requirements).
trait Fetcher:
	abstract async func fetch() -> String
	abstract func size() -> int

func test() -> void:
	var handler := func(_method_name: StringName, _args: Array) -> Variant:
		return null
	var proxy := godot.reflection.create_proxy_dynamic(Fetcher, handler) as Fetcher
	print(Callable(proxy, "fetch").is_async())
	print(Callable(proxy, "size").is_async())
